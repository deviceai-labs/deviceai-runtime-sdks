#include "deviceai/dai_session.h"
#include "json_builder.h"

#include <cstring>
#include <cstdio>
#include <string>

#define SESSION_FILE "deviceai_session.json"
#define REFRESH_WINDOW_MS (7LL * 24 * 60 * 60 * 1000)

/* ── Validity checks ───────────────────────────────────────────────────────── */

int dai_session_is_expired(const dai_session_t* session, dai_clock_fn clock_ms) {
    if (!session || !clock_ms) return 1;
    return clock_ms() > session->expires_at_ms;
}

int dai_session_needs_refresh(const dai_session_t* session, dai_clock_fn clock_ms) {
    if (!session || !clock_ms) return 0;
    return clock_ms() > (session->expires_at_ms - REFRESH_WINDOW_MS);
}

/* ── Helpers ───────────────────────────────────────────────────────────────── */

static std::string session_path(const dai_storage_t* storage) {
    char* dir = storage->models_dir(storage->ctx);
    std::string path = std::string(dir) + "/" + SESSION_FILE;
    storage->free_string(storage->ctx, dir);
    return path;
}

// Minimal JSON field extractor — finds "key":"value" or "key":number.
// Returns true and populates out_val on success. Not a full JSON parser.
static bool json_extract_str(const char* json, const char* key, char* out_val, size_t max_len) {
    // Build search pattern: "key":"
    std::string pattern = std::string("\"") + key + "\":\"";
    const char* pos = strstr(json, pattern.c_str());
    if (!pos) return false;
    pos += pattern.size();
    const char* end = strchr(pos, '"');
    if (!end) return false;
    size_t len = (size_t)(end - pos);
    if (len >= max_len) len = max_len - 1;
    strncpy(out_val, pos, len);
    out_val[len] = '\0';
    return true;
}

static bool json_extract_i64(const char* json, const char* key, int64_t* out_val) {
    std::string pattern = std::string("\"") + key + "\":";
    const char* pos = strstr(json, pattern.c_str());
    if (!pos) return false;
    pos += pattern.size();
    *out_val = (int64_t)strtoll(pos, nullptr, 10);
    return true;
}

/* ── SessionStore ──────────────────────────────────────────────────────────── */

void dai_session_store_save(const dai_storage_t* storage, const dai_session_t* session) {
    if (!storage || !session) return;

    std::string body = dai::json::ObjectBuilder()
        .str("device_id",       session->device_id)
        .str("token",           session->token)
        .i64("expires_at_ms",   session->expires_at_ms)
        .str("capability_tier", session->capability_tier)
        .build();

    std::string path = session_path(storage);
    storage->write_file(storage->ctx, path.c_str(), body.c_str());
}

int dai_session_store_load(const dai_storage_t* storage, dai_session_t* out) {
    if (!storage || !out) return 0;

    std::string path = session_path(storage);
    char* content = storage->read_file(storage->ctx, path.c_str());
    if (!content) return 0;

    bool ok =
        json_extract_str(content, "device_id",       out->device_id,       DAI_SESSION_DEVICE_ID_LEN) &&
        json_extract_str(content, "token",            out->token,           DAI_SESSION_TOKEN_LEN)     &&
        json_extract_i64(content, "expires_at_ms",   &out->expires_at_ms)                              &&
        json_extract_str(content, "capability_tier",  out->capability_tier, DAI_SESSION_TIER_LEN);

    storage->free_string(storage->ctx, content);
    return ok ? 1 : 0;
}

void dai_session_store_clear(const dai_storage_t* storage) {
    if (!storage) return;
    std::string path = session_path(storage);
    storage->delete_file(storage->ctx, path.c_str());
}
