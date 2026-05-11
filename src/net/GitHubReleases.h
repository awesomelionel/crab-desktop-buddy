#pragma once

#include "github_releases_parse.h"

namespace net {

struct FetchResult {
    bool                                    ok;
    char                                    error[64];
    github_releases_parse::ReleaseInfo      info;
};

// Synchronously fetches https://api.github.com/repos/{owner}/{repo}/releases/latest,
// validates TLS against the embedded CA bundle, and parses the response.
// `owner` and `repo` are baked at build time via macros so the call site
// stays simple.
FetchResult fetchLatestRelease();

}  // namespace net
