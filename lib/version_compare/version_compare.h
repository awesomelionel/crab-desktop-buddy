#pragma once

#include <stdint.h>

namespace version_compare {

struct Version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

// Parses a version string into `out`. Accepts:
//   - optional leading 'v'
//   - "MAJOR.MINOR.PATCH" with non-negative integer components
//   - optional pre-release suffix after a '-' (the suffix is ignored;
//     the base version is returned)
// Returns true if at least the first three integers parsed; on any
// other input writes {0, 0, 0} and still returns true (never blocks
// comparison).
bool parse(const char* s, Version& out);

// Three-way compare. Negative if a < b, zero if equal, positive if a > b.
int compare(const char* a, const char* b);

// Convenience: returns true iff parse(b) > parse(a).
bool isNewer(const char* current, const char* candidate);

}  // namespace version_compare
