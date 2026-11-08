/*
 * core/telnet.c - OS-independent telnet protocol helpers.
 * See core/telnet.h for the contract.
 */
#include "core/telnet.h"

int telnet_strip_iac(const unsigned char *in, int n, unsigned char *out)
{
    int oi = 0;
    int i = 0;

    while (i < n) {
        unsigned char c = in[i];
        if (c == 255) {                      /* IAC */
            if (i + 1 >= n) { i++; break; }  /* lone trailing IAC: drop */
            unsigned char cmd = in[i + 1];
            if (cmd == 255) {                /* escaped 0xFF */
                out[oi++] = 255;
                i += 2;
            } else if (cmd >= 251 && cmd <= 254) {
                i += 3;                      /* WILL/WONT/DO/DONT + option */
            } else {
                i += 2;                      /* other 2-byte command */
            }
        } else {
            out[oi++] = c;
            i++;
        }
    }
    return oi;
}
