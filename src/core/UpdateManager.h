#pragma once

#include <stdint.h>

#include "github_releases_parse.h"

class UpdateManager {
public:
    enum class State : uint8_t {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        InstallReady,
        Failed,
    };

    struct Status {
        State    state;
        uint32_t bytes_received;     // valid in Downloading
        uint32_t bytes_total;        // valid in Downloading
        char     last_error[64];     // valid in Failed
    };

    void begin();
    void tick(uint32_t now_ms);

    // Web endpoints call these. requestCheck() runs synchronously
    // (~3–5 s, bounded by 10-s HTTP timeout). requestInstall() returns
    // immediately; the install runs from the next tick().
    void requestCheck();
    void requestInstall();

    Status                                       status() const;
    const github_releases_parse::ReleaseInfo*    latestRelease() const;
    const char*                                  currentVersion() const;

    // Called from the HTTPUpdate progress callback. Not for general use.
    void setProgressInternal(uint32_t received, uint32_t total) {
        bytes_received_ = received;
        bytes_total_ = total;
    }

    static UpdateManager& instance();

private:
    State    state_ = State::Idle;
    uint32_t bytes_received_ = 0;
    uint32_t bytes_total_ = 0;
    char     last_error_[64] = {};

    bool     have_release_ = false;
    github_releases_parse::ReleaseInfo latest_ = {};

    bool     install_pending_ = false;

    void doCheckBlocking();
    void doInstallBlocking();
};
