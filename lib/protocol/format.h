// lib/protocol/format.h
#pragma once
#include <stddef.h>
#include <stdint.h>

// Render `n` into `buf` as a compact human-readable string suitable for
// a small display.
//
// Boundaries are pre-rounded so values never spuriously cross a width
// threshold. Worst-case rendered width is 5 chars + NUL.
//
//   n <      1 000  →  integer        e.g. "0", "7", "523"
//   n <     99 500  →  one decimal K  e.g. "1.0K", "9.9K", "31.2K", "99.4K"
//   n <    999 500  →  integer K      e.g. "100K", "523K", "999K"
//   n <  9 950 000  →  one decimal M  e.g. "1.0M", "4.3M", "9.9M"
//   n >= 9 950 000  →  integer M      e.g. "10M", "42M"
//
// `buf_len` MUST be at least 8 (room for the longest 5-char string +
// NUL + 2 spare). Returns the number of chars written (excluding NUL),
// or 0 on error (null buf, buf_len < 8).
size_t format_token_count(uint32_t n, char* buf, size_t buf_len);

// Minimum buffer size callers must allocate. Use `char buf[kFormatTokenCountBufLen];`.
constexpr size_t kFormatTokenCountBufLen = 8;
