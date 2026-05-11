#include "UpdateManager.h"

#include <string.h>

#include <Arduino.h>
#include <HTTPUpdate.h>
#include <NetworkClientSecure.h>

#include "firmware_version.h"
#include "version_compare.h"
#include "../net/GitHubReleases.h"

extern const uint8_t github_certs_pem_start[]
    asm("_binary_data_github_certs_pem_start");
extern const uint8_t github_certs_pem_end[]
    asm("_binary_data_github_certs_pem_end");

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
    if (!have_release_ || !latest_.download_url[0]) {
        strncpy(last_error_, "no release", sizeof(last_error_) - 1);
        state_ = State::Failed;
        return;
    }

    NetworkClientSecure client;
    const size_t bundle_size =
        (size_t)(github_certs_pem_end - github_certs_pem_start);
    client.setCACertBundle(github_certs_pem_start, bundle_size);

    httpUpdate.rebootOnUpdate(false);
    // GitHub release-asset URLs 302 to objects.githubusercontent.com.
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.onProgress([](int cur, int total) {
        UpdateManager::instance().setProgressInternal(
            (uint32_t)cur, (uint32_t)total);
    });

    t_httpUpdate_return result =
        httpUpdate.update(client, latest_.download_url, FIRMWARE_VERSION);

    if (result == HTTP_UPDATE_OK) {
        state_ = State::InstallReady;
        delay(500);
        ESP.restart();   // never returns
        return;
    }

    const String& msg = httpUpdate.getLastErrorString();
    strncpy(last_error_, msg.c_str(), sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = 0;
    state_ = State::Failed;
}
