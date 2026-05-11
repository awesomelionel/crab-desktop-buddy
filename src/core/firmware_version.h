#pragma once

// Stamped at compile time. CI sets FW_VERSION from the tag name (without
// the leading 'v'); local builds inherit the default "0.0.0-dev".
constexpr const char* FIRMWARE_VERSION =
#ifdef FW_VERSION
    FW_VERSION;
#else
    "0.0.0-dev";
#endif
