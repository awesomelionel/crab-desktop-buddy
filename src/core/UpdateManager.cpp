#include "UpdateManager.h"

#include <string.h>

#include "firmware_version.h"
#include "version_compare.h"
#include "../net/GitHubReleases.h"

UpdateManager& UpdateManager::instance() {
    static UpdateManager s;
    return s;
}

void UpdateManager::begin() {
    state_ = State::Idle;
    bytes_received_ = 0;
    bytes_total_ = 0;
    last_error_[0] = 0;
    have_release_ = false;
    install_pending_ = false;
}

void UpdateManager::tick(uint32_t /*now_ms*/) {
    if (install_pending_) {
        install_pending_ = false;
        doInstallBlocking();
    }
}

void UpdateManager::requestCheck() {
    doCheckBlocking();
}

void UpdateManager::requestInstall() {
    if (state_ != State::UpdateAvailable) {
        strncpy(last_error_,
                "no update available", sizeof(last_error_) - 1);
        state_ = State::Failed;
        return;
    }
    state_ = State::Downloading;
    bytes_received_ = 0;
    bytes_total_ = 0;
    install_pending_ = true;
}

UpdateManager::Status UpdateManager::status() const {
    Status s{};
    s.state = state_;
    s.bytes_received = bytes_received_;
    s.bytes_total = bytes_total_;
    strncpy(s.last_error, last_error_, sizeof(s.last_error) - 1);
    s.last_error[sizeof(s.last_error) - 1] = 0;
    return s;
}

const github_releases_parse::ReleaseInfo*
UpdateManager::latestRelease() const {
    return have_release_ ? &latest_ : nullptr;
}

const char* UpdateManager::currentVersion() const {
    return FIRMWARE_VERSION;
}

void UpdateManager::doCheckBlocking() {
    state_ = State::Checking;
    have_release_ = false;
    last_error_[0] = 0;

    net::FetchResult r = net::fetchLatestRelease();
    if (!r.ok) {
        strncpy(last_error_, r.error, sizeof(last_error_) - 1);
        last_error_[sizeof(last_error_) - 1] = 0;
        state_ = State::Failed;
        return;
    }

    latest_ = r.info;
    have_release_ = true;

    if (version_compare::isNewer(FIRMWARE_VERSION, latest_.tag)) {
        state_ = State::UpdateAvailable;
    } else {
        state_ = State::UpToDate;
    }
}

void UpdateManager::doInstallBlocking() {
    // Placeholder until Task 9 lands the real install. For now: mark
    // failed so the state machine doesn't hang in Downloading if some
    // caller manages to invoke install_pending before Task 9.
    strncpy(last_error_, "install not implemented",
            sizeof(last_error_) - 1);
    state_ = State::Failed;
}
