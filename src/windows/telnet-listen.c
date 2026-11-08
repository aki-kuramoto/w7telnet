/*
 * w7telnet-listen - a temporary, unauthenticated telnet listener for Windows 7
 *
 * This is NOT a daemon. It is meant to be started by hand for a short while, on
 * a network you already know is safe, to get a first shell on an old host so you
 * can do some work - then stopped. It hands cmd.exe to whoever connects, with no
 * authentication. Make sure you fully understand which hosts can reach it and
 * what level of access you are granting before running it. If that is not clear
 * to you, do not use it.
 *
 * It can be built two ways, both producing a self-contained exe that runs on a
 * stock Windows 7 with no extra runtime:
 *   - gcc / mingw-w64, CRT stripped (-ffreestanding -nostdlib
 *     -e mainCRTStartup): uses only Win32 APIs, so the only dependencies are
 *     KERNEL32.dll and WS2_32.dll. This is the primary build.
 *   - MSVC (cl /MT): links the C runtime statically, so again no VC runtime
 *     or UCRT DLLs are required.
 * See the project Makefile / src\windows\build.bat / build-msvc.bat.
 *
 * Usage: w7telnet-listen-x64.exe [port] [bind-address]
 *   e.g. w7telnet-listen-x64.exe              -> listen on 0.0.0.0:23
 *        w7telnet-listen-x64.exe 2323         -> 0.0.0.0:2323
 *        w7telnet-listen-x64.exe 23 127.0.0.1 -> localhost only
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS 1   /* allow inet_addr() under MSVC */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "core/telnet.h"

#define BUFSZ 4096

#if defined(_MSC_VER)

/* MSVC build: pull memset/memcpy from the (statically linked) CRT, and let
 * the linker find Winsock automatically. */
#include <string.h>
#pragma comment(lib, "ws2_32.lib")

#else

/* CRT-free gcc build: provide the primitives gcc may emit implicit calls to
 * (e.g. memset/memcpy for struct initialization). */
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

#endif

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Write a string to standard output (for logging). */
static void outstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;
    if (h && h != INVALID_HANDLE_VALUE)
        WriteFile(h, s, (DWORD)slen(s), &w, NULL);
}

/* Format an unsigned integer as a decimal string (NUL-terminated). */
static void utoa10(unsigned int v, char *buf)
{
    char tmp[16];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* Minimal string-to-int (leading whitespace + digits only). */
static int my_atoi(const char *s)
{
    int v = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* --- Per-connection state --- */
typedef struct {
    SOCKET  sock;
    HANDLE  childIn;
    HANDLE  childOut;
    HANDLE  hProcess;
    volatile LONG done;
} Session;

/* cmd.exe output -> socket */
static DWORD WINAPI pumpChildToSock(LPVOID arg)
{
    Session *s = (Session *)arg;
    char buf[BUFSZ];
    DWORD n;

    while (ReadFile(s->childOut, buf, sizeof(buf), &n, NULL) && n > 0) {
        char *p = buf;
        DWORD left = n;
        while (left > 0) {
            int w = send(s->sock, p, (int)left, 0);
            if (w <= 0) goto out;
            p += w;
            left -= (DWORD)w;
        }
    }
out:
    InterlockedExchange(&s->done, 1);
    shutdown(s->sock, SD_BOTH);
    return 0;
}

/*
 * socket -> cmd.exe stdin.
 * Telnet IAC (0xFF) negotiation sequences are stripped before the data is
 * forwarded to cmd.exe (see core/telnet.c).
 */
static DWORD WINAPI pumpSockToChild(LPVOID arg)
{
    Session *s = (Session *)arg;
    unsigned char in[BUFSZ];
    unsigned char out[BUFSZ];

    for (;;) {
        int n = recv(s->sock, (char *)in, sizeof(in), 0);
        if (n <= 0) break;

        int oi = telnet_strip_iac(in, n, out);

        if (oi > 0) {
            DWORD w;
            if (!WriteFile(s->childIn, out, (DWORD)oi, &w, NULL) || w == 0)
                break;
        }
    }

    InterlockedExchange(&s->done, 1);
    CloseHandle(s->childIn);
    s->childIn = NULL;
    return 0;
}

/* Handle a single connection. */
static void handleClient(SOCKET sock)
{
    Session s;
    memset(&s, 0, sizeof(s));
    s.sock = sock;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE inRd = NULL, inWr = NULL;
    HANDLE outRd = NULL, outWr = NULL;

    if (!CreatePipe(&inRd, &inWr, &sa, 0) ||
        !CreatePipe(&outRd, &outWr, &sa, 0)) {
        goto cleanup;
    }
    SetHandleInformation(inWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = inRd;
    si.hStdOutput = outWr;
    si.hStdError  = outWr;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    char cmdline[] = "cmd.exe";
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        const char *msg = "w7telnet: failed to start cmd.exe\r\n";
        send(sock, msg, slen(msg), 0);
        goto cleanup;
    }

    CloseHandle(inRd);  inRd = NULL;
    CloseHandle(outWr); outWr = NULL;

    s.childIn  = inWr;  inWr = NULL;
    s.childOut = outRd; outRd = NULL;
    s.hProcess = pi.hProcess;

    HANDLE t1 = CreateThread(NULL, 0, pumpChildToSock, &s, 0, NULL);
    HANDLE t2 = CreateThread(NULL, 0, pumpSockToChild, &s, 0, NULL);

    HANDLE waits[3];
    int nw = 0;
    waits[nw++] = pi.hProcess;
    if (t1) waits[nw++] = t1;
    if (t2) waits[nw++] = t2;
    WaitForMultipleObjects(nw, waits, FALSE, INFINITE);

    TerminateProcess(pi.hProcess, 0);
    shutdown(sock, SD_BOTH);
    closesocket(sock);
    s.sock = INVALID_SOCKET;

    if (t1) { WaitForSingleObject(t1, 2000); CloseHandle(t1); }
    if (t2) { WaitForSingleObject(t2, 2000); CloseHandle(t2); }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (s.childIn)  CloseHandle(s.childIn);
    if (s.childOut) CloseHandle(s.childOut);
    return;

cleanup:
    if (inRd)  CloseHandle(inRd);
    if (inWr)  CloseHandle(inWr);
    if (outRd) CloseHandle(outRd);
    if (outWr) CloseHandle(outWr);
    if (sock != INVALID_SOCKET) closesocket(sock);
}

static DWORD WINAPI clientThread(LPVOID arg)
{
    SOCKET sock = (SOCKET)(UINT_PTR)arg;
    handleClient(sock);
    return 0;
}

/*
 * Lightly parse GetCommandLineA() into argv[1]=port, argv[2]=bind-address.
 * argv[0] is skipped (quote-aware).
 */
static void parseArgs(unsigned short *port, char *bindbuf, int bindbufsz)
{
    const char *p = GetCommandLineA();

    /* skip argv[0] */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') p++;
    }

    /* token 1: port */
    while (*p == ' ' || *p == '\t') p++;
    if (*p >= '0' && *p <= '9') {
        int v = my_atoi(p);
        if (v > 0 && v < 65536) *port = (unsigned short)v;
        while (*p && *p != ' ' && *p != '\t') p++;
    } else {
        return;
    }

    /* token 2: bind address */
    while (*p == ' ' || *p == '\t') p++;
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < bindbufsz - 1)
        bindbuf[i++] = *p++;
    bindbuf[i] = 0;
}

/*
 * Entry point. The gcc build links no CRT and jumps straight to
 * mainCRTStartup; the MSVC build links the CRT, whose own mainCRTStartup
 * performs runtime init and then calls main().
 */
#if defined(_MSC_VER)
int main(void)
#else
void mainCRTStartup(void)
#endif
{
    unsigned short port = 23;
    char bindaddr[64];
    bindaddr[0] = 0;

    parseArgs(&port, bindaddr, sizeof(bindaddr));
    if (bindaddr[0] == 0) {
        bindaddr[0]='0'; bindaddr[1]='.'; bindaddr[2]='0'; bindaddr[3]='.';
        bindaddr[4]='0'; bindaddr[5]='.'; bindaddr[6]='0'; bindaddr[7]=0;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        outstr("WSAStartup failed\r\n");
        ExitProcess(1);
    }

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        outstr("socket failed\r\n");
        WSACleanup();
        ExitProcess(1);
    }

    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(bindaddr);
    if (addr.sin_addr.s_addr == INADDR_NONE)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        outstr("bind failed\r\n");
        closesocket(ls);
        WSACleanup();
        ExitProcess(1);
    }

    if (listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        outstr("listen failed\r\n");
        closesocket(ls);
        WSACleanup();
        ExitProcess(1);
    }

    {
        char pnum[16];
        utoa10(port, pnum);
        outstr("w7telnet: listening on ");
        outstr(bindaddr);
        outstr(":");
        outstr(pnum);
        outstr(" (no auth)\r\n");
    }

    for (;;) {
        struct sockaddr_in ca;
        int calen = sizeof(ca);
        SOCKET cs = accept(ls, (struct sockaddr *)&ca, &calen);
        if (cs == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEINTR) continue;
            break;
        }
        outstr("w7telnet: client connected\r\n");

        HANDLE th = CreateThread(NULL, 0, clientThread,
                                 (LPVOID)(UINT_PTR)cs, 0, NULL);
        if (th) CloseHandle(th);
        else    handleClient(cs);
    }

    closesocket(ls);
    WSACleanup();
    ExitProcess(0);
}
