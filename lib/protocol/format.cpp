#include "format.h"
#include <stdio.h>

size_t format_token_count(uint32_t n, char* buf, size_t buf_len) {
    if (!buf || buf_len < kFormatTokenCountBufLen) {
        if (buf && buf_len > 0) buf[0] = 0;
        return 0;
    }

    int written;
    if (n < 1000) {
        // integer: "0".."999"
        written = snprintf(buf, buf_len, "%u", (unsigned)n);
    } else if (n < 99500) {
        // one decimal K: "1.0K".."99.4K"
        // Round half-up at the tenths place.
        unsigned tenths = (unsigned)((n + 50) / 100);   // n/100, rounded
        unsigned whole  = tenths / 10;
        unsigned frac   = tenths % 10;
        written = snprintf(buf, buf_len, "%u.%uK", whole, frac);
    } else if (n < 999500) {
        // integer K: "100K".."999K"
        // Round half-up at the unit place (1000).
        unsigned k = (unsigned)((n + 500) / 1000);
        written = snprintf(buf, buf_len, "%uK", k);
    } else if (n < 9950000) {
        // one decimal M: "1.0M".."9.9M"
        // Round half-up at the tenths-of-million (i.e. 100_000).
        unsigned tenths = (unsigned)((n + 50000) / 100000);
        unsigned whole  = tenths / 10;
        unsigned frac   = tenths % 10;
        written = snprintf(buf, buf_len, "%u.%uM", whole, frac);
    } else {
        // integer M: "10M" and above
        // Round half-up at the unit place (1_000_000).
        unsigned m = (unsigned)((n + 500000) / 1000000);
        written = snprintf(buf, buf_len, "%uM", m);
    }

    if (written < 0 || (size_t)written >= buf_len) {
        buf[0] = 0;
        return 0;
    }
    return (size_t)written;
}
