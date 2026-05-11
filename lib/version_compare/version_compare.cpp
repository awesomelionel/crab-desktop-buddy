#include "version_compare.h"

#include <stddef.h>

namespace version_compare {

namespace {

// Reads decimal digits from *p into *out. Returns true if at least one
// digit was consumed. Advances *p past the digits.
bool readUint(const char*& p, uint16_t& out) {
    if (!p || *p < '0' || *p > '9') return false;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        if (v > 0xFFFF) v = 0xFFFF;
        ++p;
    }
    out = (uint16_t)v;
    return true;
}

}  // namespace

bool parse(const char* s, Version& out) {
    out = {0, 0, 0};
    if (!s) return true;
    if (*s == 'v' || *s == 'V') ++s;

    uint16_t maj = 0, min = 0, pat = 0;
    const char* p = s;
    if (!readUint(p, maj))     return true;   // already zeroed
    if (*p++ != '.')           return true;
    if (!readUint(p, min))     return true;
    if (*p++ != '.')           return true;
    if (!readUint(p, pat))     return true;

    out = {maj, min, pat};
    return true;
}

int compare(const char* a, const char* b) {
    Version va, vb;
    parse(a, va);
    parse(b, vb);
    if (va.major != vb.major) return (va.major < vb.major) ? -1 : 1;
    if (va.minor != vb.minor) return (va.minor < vb.minor) ? -1 : 1;
    if (va.patch != vb.patch) return (va.patch < vb.patch) ? -1 : 1;
    return 0;
}

bool isNewer(const char* current, const char* candidate) {
    return compare(current, candidate) < 0;
}

}  // namespace version_compare
