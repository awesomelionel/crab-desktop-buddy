#include "GitHubReleases.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <string.h>

extern const uint8_t github_certs_pem_start[]
    asm("_binary_data_github_certs_pem_start");
extern const uint8_t github_certs_pem_end[]
    asm("_binary_data_github_certs_pem_end");

namespace net {

namespace {

constexpr uint32_t kHttpTimeoutMs = 10000;

void setError(FetchResult& r, const char* msg) {
    r.ok = false;
    strncpy(r.error, msg, sizeof(r.error) - 1);
    r.error[sizeof(r.error) - 1] = 0;
}

}  // namespace

FetchResult fetchLatestRelease() {
    FetchResult r{};
    r.ok = false;

    NetworkClientSecure client;
    const size_t bundle_size =
        (size_t)(github_certs_pem_end - github_certs_pem_start);
    client.setCACertBundle(github_certs_pem_start, bundle_size);

    char url[160];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/%s/releases/latest",
             GITHUB_OWNER, GITHUB_REPO);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        setError(r, "http begin failed");
        return r;
    }
    http.addHeader("User-Agent", "claude-buddy-ota/1");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http %d", code);
        setError(r, msg);
        http.end();
        return r;
    }

    String body = http.getString();
    http.end();

    char perr[64] = {};
    if (!github_releases_parse::parse(body.c_str(), r.info,
                                       perr, sizeof(perr))) {
        setError(r, perr[0] ? perr : "parse failed");
        return r;
    }

    r.ok = true;
    return r;
}

}  // namespace net
