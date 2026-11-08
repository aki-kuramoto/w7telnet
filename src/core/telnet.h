/*
 * core/telnet.h - OS-independent telnet protocol helpers.
 *
 * Shared by every platform backend (windows/, posix/). Contains only pure
 * byte manipulation, so it compiles both in the freestanding Windows build
 * (no C runtime) and in the normal libc POSIX build.
 */
#ifndef CORE_TELNET_H
#define CORE_TELNET_H

/*
 * Strip telnet IAC (0xFF) negotiation sequences from `in` (length `n` bytes)
 * and write the remaining data bytes to `out`. `out` must have room for at
 * least `n` bytes. Returns the number of bytes written to `out`.
 *
 * Handling:
 *   - IAC IAC (0xFF 0xFF) is an escaped 0xFF -> one 0xFF byte is emitted.
 *   - IAC WILL/WONT/DO/DONT (251..254) consumes a 3-byte sequence.
 *   - Any other IAC command consumes a 2-byte sequence.
 *   - A lone trailing IAC with no following byte is dropped.
 *
 * Note: negotiation sequences that straddle two reads are not reassembled;
 * this matches the original single-buffer behavior and is adequate for the
 * simple clients this server targets.
 */
int telnet_strip_iac(const unsigned char *in, int n, unsigned char *out);

#endif /* CORE_TELNET_H */
