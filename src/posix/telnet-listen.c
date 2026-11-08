/*
 * w7telnet-listen - a temporary, unauthenticated telnet listener for macOS /
 *                   Linux and other POSIX systems (counterpart of the Windows
 *                   build).
 *
 * This is NOT a daemon. It is meant to be started by hand for a short while, on
 * a network you already know is safe, to get a first shell on a host so you can
 * do some work - then stopped. It hands an interactive shell to whoever
 * connects, with no authentication. Make sure you fully understand which hosts
 * can reach it and what level of access you are granting before running it. If
 * that is not clear to you, do not use it.
 *
 * The shell is run inside a pseudo-terminal (forkpty) so it behaves like a
 * real interactive session. By default the connecting user's login shell is
 * used (looked up from the password database, then $SHELL, then /bin/sh);
 * it can be overridden with -s.
 *
 * Usage: w7telnet-listen [-s shell] [port] [bind-address]
 *   e.g. w7telnet-listen                   -> listen on 0.0.0.0:23
 *        w7telnet-listen 2323              -> 0.0.0.0:2323
 *        w7telnet-listen 2323 127.0.0.1    -> localhost only
 *        w7telnet-listen -s /bin/bash 2323 -> force /bin/bash
 *
 * Build - see the project Makefile:
 *   cc -O2 -Wall -Wextra -Isrc -o w7telnet-listen \
 *     src/posix/telnet-listen.c src/core/telnet.c -lpthread  (+ -lutil on Linux)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* forkpty() lives in different headers across platforms. */
#if defined(__APPLE__)
#  include <util.h>
#elif defined(__linux__)
#  include <pty.h>
#else
#  include <libutil.h>   /* most BSDs */
#endif

#include "core/telnet.h"

#define BUFSZ 4096

/* Shell override from -s, or NULL to auto-detect per connection. */
static const char *g_shell_override = NULL;

/* Determine the shell to hand out: login shell, then $SHELL, then /bin/sh. */
static const char *detect_shell(void)
{
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_shell && pw->pw_shell[0])
        return pw->pw_shell;
    const char *s = getenv("SHELL");
    if (s && s[0])
        return s;
    return "/bin/sh";
}

/* Write all `len` bytes of `buf` to fd (socket send). Returns 0 on success. */
static int send_all(int sock, const unsigned char *buf, int len)
{
    int off = 0;
    while (off < len) {
        ssize_t w = send(sock, buf + off, (size_t)(len - off), 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        off += (int)w;
    }
    return 0;
}

/* Write all `len` bytes of `buf` to fd (pty master write). Returns 0 on success. */
static int write_all(int fd, const unsigned char *buf, int len)
{
    int off = 0;
    while (off < len) {
        ssize_t w = write(fd, buf + off, (size_t)(len - off));
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        off += (int)w;
    }
    return 0;
}

/* Run a shell on a pty and relay bytes between the socket and the pty. */
static void relay(int sock)
{
    int master = -1;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);
    if (pid < 0) {
        const char *m = "w7telnet: forkpty failed\r\n";
        send(sock, m, strlen(m), 0);
        return;
    }

    if (pid == 0) {
        /* child: become the shell */
        const char *sh = g_shell_override ? g_shell_override : detect_shell();
        execl(sh, sh, "-i", (char *)NULL);
        _exit(127);
    }

    /* parent: pump data both ways until either side closes */
    struct pollfd fds[2];
    fds[0].fd = sock;
    fds[1].fd = master;

    unsigned char in[BUFSZ];
    unsigned char out[BUFSZ];

    for (;;) {
        fds[0].events = POLLIN;
        fds[1].events = POLLIN;

        int r = poll(fds, 2, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & POLLIN) {                 /* socket -> shell */
            ssize_t n = recv(sock, in, sizeof(in), 0);
            if (n <= 0) break;
            int oi = telnet_strip_iac(in, (int)n, out);
            if (oi > 0 && write_all(master, out, oi) != 0) break;
        }

        if (fds[1].revents & POLLIN) {                 /* shell -> socket */
            ssize_t n = read(master, out, sizeof(out));
            if (n <= 0) break;
            if (send_all(sock, out, (int)n) != 0) break;
        }

        if ((fds[0].revents | fds[1].revents) & (POLLHUP | POLLERR | POLLNVAL))
            break;
    }

    close(master);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

static void *client_thread(void *arg)
{
    int sock = (int)(intptr_t)arg;
    relay(sock);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return NULL;
}

/* Parse [-s shell] [port] [bind-address]. */
static void parse_args(int argc, char **argv,
                       unsigned short *port, const char **bindaddr)
{
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            g_shell_override = argv[++i];
        } else if (positional == 0) {
            int v = atoi(argv[i]);
            if (v > 0 && v < 65536) *port = (unsigned short)v;
            positional = 1;
        } else if (positional == 1) {
            *bindaddr = argv[i];
            positional = 2;
        }
    }
}

int main(int argc, char **argv)
{
    unsigned short port = 23;
    const char *bindaddr = "0.0.0.0";

    parse_args(argc, argv, &port, &bindaddr);

    /* A write to a closed socket must not kill the server. */
    signal(SIGPIPE, SIG_IGN);

    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bindaddr, &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(ls);
        return 1;
    }
    if (listen(ls, SOMAXCONN) < 0) {
        perror("listen");
        close(ls);
        return 1;
    }

    fprintf(stderr, "w7telnet: listening on %s:%u (no auth)\n",
            bindaddr, (unsigned)port);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t calen = sizeof(ca);
        int cs = accept(ls, (struct sockaddr *)&ca, &calen);
        if (cs < 0) {
            if (errno == EINTR) continue;
            break;
        }
        fprintf(stderr, "w7telnet: client connected\n");

        pthread_t t;
        if (pthread_create(&t, NULL, client_thread, (void *)(intptr_t)cs) == 0)
            pthread_detach(t);
        else
            close(cs);
    }

    close(ls);
    return 0;
}
