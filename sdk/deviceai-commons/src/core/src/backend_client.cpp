#include "deviceai/dai_backend.h"
#include "json_builder.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>

/* ── Logging helper ────────────────────────────────────────────────────────── */

static void log(const dai_platform_t* p, dai_log_level_t level, const char* msg) {
    if (p && p->log) {
        p->log(level, "BackendClient", msg);
    } else {
        fprintf(stderr, "[BackendClient] %s\n", msg);
    }
}

/* ── Header helpers ────────────────────────────────────────────────────────── */

// Build standard headers: Authorization + Content-Type + SDK metadata
struct Headers {
    dai_http_header_t arr[4];
    int count = 0;
    std::string auth_val;   // must outlive the header array

    void bearer(const char* token) {
        auth_val = std::string("Bearer ") + token;
        arr[count++] = { "Authorization", auth_val.c_str() };
    }
    void json_content_type() {
        arr[count++] = { "Content-Type", "application/json" };
    }
};

/* ── JSON response helpers ─────────────────────────────────────────────────── */

static bool extract_str(const char* json, const char* key, char* out, size_t max_len) {
    std::string pattern = std::string("\"") + key + "\":\"";
    const char* pos = json ? strstr(json, pattern.c_str()) : nullptr;
    if (!pos) return false;
    pos += pattern.size();
    const char* end = strchr(pos, '"');
    if (!end) return false;
    size_t len = std::min((size_t)(end - pos), max_len - 1);
    strncpy(out, pos, len);
    out[len] = '\0';
    return true;
}

static bool extract_i64(const char* json, const char* key, int64_t* out) {
    std::string pattern = std::string("\"") + key + "\":";
    const char* pos = json ? strstr(json, pattern.c_str()) : nullptr;
    if (!pos) return false;
    pos += pattern.size();
    *out = (int64_t)strtoll(pos, nullptr, 10);
    return true;
}

static bool extract_int(const char* json, const char* key, int* out) {
    int64_t v = 0;
    if (!extract_i64(json, key, &v)) return false;
    *out = (int)v;
    return true;
}

/* ── Event serialization ───────────────────────────────────────────────────── */

static std::string serialize_event(const dai_event_t* ev) {
    dai::json::ObjectBuilder b;
    b.i64("timestamp_ms", ev->timestamp_ms);

    switch (ev->type) {
        case DAI_EVENT_MODEL_LOAD:
            b.str("type", "model_load");
            b.str("module",   ev->model_load.module);
            b.str("model_id", ev->model_load.model_id);
            b.i64("duration_ms", ev->model_load.duration_ms);
            if (ev->model_load.has_ram_delta)
                b.f32("ram_delta_mb", ev->model_load.ram_delta_mb);
            break;

        case DAI_EVENT_MODEL_UNLOAD:
            b.str("type", "model_unload");
            b.str("module",   ev->model_unload.module);
            b.str("model_id", ev->model_unload.model_id);
            break;

        case DAI_EVENT_INFERENCE_COMPLETE: {
            const auto& inf = ev->inference;
            b.str("type", "inference_complete");
            b.str("module",    inf.module);
            b.str("model_id",  inf.model_id);
            b.i64("latency_ms", inf.latency_ms);
            if (inf.has_ttft)            b.i64("ttft_ms",           inf.ttft_ms);
            if (inf.has_tokens_per_sec)  b.f32("tokens_per_sec",    inf.tokens_per_sec);
            if (inf.has_input_tokens)    b.i32("input_token_count",  inf.input_token_count);
            if (inf.has_output_tokens)   b.i32("output_token_count", inf.output_token_count);
            if (inf.has_input_length_ms) b.i32("input_length_ms",   inf.input_length_ms);
            if (inf.has_output_chars)    b.i32("output_chars",       inf.output_chars);
            if (inf.finish_reason[0])    b.str("finish_reason",     inf.finish_reason);
            break;
        }

        case DAI_EVENT_OTA_DOWNLOAD: {
            const auto& ota = ev->ota_download;
            b.str("type", "ota_download");
            b.str("model_id",    ota.model_id);
            b.str("version",     ota.version);
            b.i64("size_bytes",  ota.size_bytes);
            b.i64("duration_ms", ota.duration_ms);
            b.boolean("success", ota.success != 0);
            if (ota.error_code[0]) b.str("error_code", ota.error_code);
            break;
        }

        case DAI_EVENT_MANIFEST_SYNC: {
            const auto& ms = ev->manifest_sync;
            b.str("type", "manifest_sync");
            b.boolean("success", ms.success != 0);
            b.i32("model_count", ms.model_count);
            if (ms.error_code[0]) b.str("error_code", ms.error_code);
            break;
        }

        case DAI_EVENT_CONTROL_PLANE_ALERT: {
            const auto& cpa = ev->control_plane_alert;
            b.str("type", "control_plane_alert");
            b.str("alert_type", cpa.alert_type);
            if (cpa.model_id[0])   b.str("model_id",   cpa.model_id);
            if (cpa.rollout_id[0]) b.str("rollout_id", cpa.rollout_id);
            break;
        }
    }

    return b.build();
}

/* ── Register device ───────────────────────────────────────────────────────── */

dai_error_t dai_backend_register_device(
    const char*                base_url,
    const char*                api_key,
    const dai_capability_kv_t* profile,
    int                        profile_count,
    const dai_platform_t*      platform,
    dai_session_t*             out_session
) {
    if (!base_url || !api_key || !platform || !out_session) return DAI_ERR_INVALID_ARG;

    // Build capability_profile JSON object
    dai::json::ObjectBuilder profile_obj;
    for (int i = 0; i < profile_count; i++) {
        profile_obj.raw(profile[i].key, profile[i].value_json);
    }

    std::string body = dai::json::ObjectBuilder()
        .raw("capability_profile", profile_obj.build())
        .build();

    std::string url = std::string(base_url) + "/v1/devices/register";
    Headers h;
    h.bearer(api_key);
    h.json_content_type();

    dai_http_response_t* resp = platform->http.post(
        platform->http.ctx, url.c_str(), body.c_str(), h.arr, h.count);

    if (!resp) return DAI_ERR_NETWORK;

    dai_error_t result = DAI_OK;
    if (resp->status_code == 401) {
        result = DAI_ERR_UNAUTHORIZED;
    } else if (resp->status_code < 200 || resp->status_code >= 300) {
        char msg[128];
        snprintf(msg, sizeof(msg), "register returned HTTP %d", resp->status_code);
        log(platform, DAI_LOG_WARN, msg);
        result = DAI_ERR_HTTP;
    } else if (resp->body) {
        bool ok =
            extract_str(resp->body, "device_id",       out_session->device_id, DAI_SESSION_DEVICE_ID_LEN) &&
            extract_str(resp->body, "token",            out_session->token,     DAI_SESSION_TOKEN_LEN);
        extract_str(resp->body, "capability_tier",  out_session->capability_tier, DAI_SESSION_TIER_LEN);
        if (!ok) result = DAI_ERR_PARSE;
        // expires_at_ms set by caller (now + 30 days)
        if (platform->clock_ms) {
            out_session->expires_at_ms = platform->clock_ms() + (30LL * 24 * 60 * 60 * 1000);
        }
        if (!out_session->capability_tier[0]) {
            strncpy(out_session->capability_tier, "mid", DAI_SESSION_TIER_LEN - 1);
        }
    } else {
        result = DAI_ERR_PARSE;
    }

    platform->http.free_response(platform->http.ctx, resp);
    return result;
}

/* ── Fetch manifest ────────────────────────────────────────────────────────── */

dai_error_t dai_backend_fetch_manifest(
    const char*           base_url,
    const char*           device_token,
    const dai_platform_t* platform,
    dai_manifest_t*       out_manifest
) {
    if (!base_url || !device_token || !platform || !out_manifest) return DAI_ERR_INVALID_ARG;

    std::string url = std::string(base_url) + "/v1/manifest";
    Headers h;
    h.bearer(device_token);

    dai_http_response_t* resp = platform->http.get(
        platform->http.ctx, url.c_str(), h.arr, h.count);

    if (!resp) return DAI_ERR_NETWORK;

    dai_error_t result = DAI_OK;
    if (resp->status_code == 401) {
        result = DAI_ERR_UNAUTHORIZED;
    } else if (resp->status_code < 200 || resp->status_code >= 300) {
        result = DAI_ERR_HTTP;
    } else if (resp->body) {
        // Parse rollout_id
        extract_str(resp->body, "rollout_id", out_manifest->rollout_id,
                    sizeof(out_manifest->rollout_id));
        if (platform->clock_ms)
            out_manifest->fetched_at_ms = platform->clock_ms();

        // Parse entries array — find "entries":[...]
        // This is a best-effort incremental parser for the known schema.
        const char* arr_start = strstr(resp->body, "\"entries\":[");
        if (arr_start) {
            arr_start = strchr(arr_start, '[') + 1;
            const char* cursor = arr_start;
            out_manifest->entry_count = 0;

            while (*cursor && out_manifest->entry_count < DAI_MANIFEST_MAX_ENTRIES) {
                const char* obj_start = strchr(cursor, '{');
                if (!obj_start) break;
                const char* obj_end = strchr(obj_start, '}');
                if (!obj_end) break;

                // Extract into a null-terminated sub-string for extract_* helpers
                size_t obj_len = (size_t)(obj_end - obj_start) + 1;
                std::string obj_str(obj_start, obj_len);
                const char* obj = obj_str.c_str();

                dai_manifest_entry_t& entry = out_manifest->entries[out_manifest->entry_count];
                memset(&entry, 0, sizeof(entry));

                extract_str(obj, "model_id",       entry.model_id,         DAI_MANIFEST_MODEL_ID_LEN);
                extract_str(obj, "download_url",   entry.download_url,     DAI_MANIFEST_URL_LEN);
                extract_str(obj, "checksum_sha256",entry.checksum_sha256,  DAI_MANIFEST_CHECKSUM_LEN);
                extract_str(obj, "format",         entry.format,           DAI_MANIFEST_FORMAT_LEN);
                extract_str(obj, "min_app_version",entry.min_app_version,  sizeof(entry.min_app_version));
                int64_t sz = 0; extract_i64(obj, "size_bytes", &sz); entry.size_bytes = sz;
                extract_int(obj, "is_required",    &entry.is_required);
                extract_int(obj, "kill_switch",    &entry.kill_switch);

                out_manifest->entry_count++;
                cursor = obj_end + 1;
            }
        }
    } else {
        result = DAI_ERR_PARSE;
    }

    platform->http.free_response(platform->http.ctx, resp);
    return result;
}

/* ── Ingest telemetry ──────────────────────────────────────────────────────── */

dai_error_t dai_backend_ingest_telemetry(
    const char*           base_url,
    const char*           device_token,
    const char*           session_id,
    const dai_event_t*    events,
    int                   event_count,
    const dai_platform_t* platform
) {
    if (!base_url || !device_token || !events || event_count <= 0 || !platform)
        return DAI_ERR_INVALID_ARG;

    int batch_size = std::min(event_count, 500);

    dai::json::ArrayBuilder arr;
    for (int i = 0; i < batch_size; i++) {
        arr.item(serialize_event(&events[i]));
    }

    std::string body = dai::json::ObjectBuilder()
        .str("session_id", session_id ? session_id : "")
        .raw("events", arr.build())
        .build();

    std::string url = std::string(base_url) + "/v1/telemetry/batch";
    Headers h;
    h.bearer(device_token);
    h.json_content_type();

    dai_http_response_t* resp = platform->http.post(
        platform->http.ctx, url.c_str(), body.c_str(), h.arr, h.count);

    if (!resp) return DAI_ERR_NETWORK;

    dai_error_t result = DAI_OK;
    if (resp->status_code < 200 || resp->status_code >= 300) {
        char msg[128];
        snprintf(msg, sizeof(msg), "telemetry batch returned HTTP %d", resp->status_code);
        log(platform, DAI_LOG_WARN, msg);
        result = (resp->status_code == 401) ? DAI_ERR_UNAUTHORIZED : DAI_ERR_HTTP;
    }

    platform->http.free_response(platform->http.ctx, resp);
    return result;
}

/* ── Refresh token ─────────────────────────────────────────────────────────── */

dai_error_t dai_backend_refresh_token(
    const char*           base_url,
    const dai_session_t*  session,
    const dai_platform_t* platform,
    dai_session_t*        out_session
) {
    if (!base_url || !session || !platform || !out_session) return DAI_ERR_INVALID_ARG;

    std::string url = std::string(base_url) + "/v1/devices/refresh";
    Headers h;
    h.bearer(session->token);
    h.json_content_type();

    dai_http_response_t* resp = platform->http.post(
        platform->http.ctx, url.c_str(), nullptr, h.arr, h.count);

    if (!resp) return DAI_ERR_NETWORK;

    dai_error_t result = DAI_OK;
    if (resp->status_code < 200 || resp->status_code >= 300) {
        result = (resp->status_code == 401) ? DAI_ERR_UNAUTHORIZED : DAI_ERR_HTTP;
    } else if (resp->body) {
        *out_session = *session; // copy existing fields
        if (!extract_str(resp->body, "token", out_session->token, DAI_SESSION_TOKEN_LEN)) {
            result = DAI_ERR_PARSE;
        } else if (platform->clock_ms) {
            out_session->expires_at_ms = platform->clock_ms() + (30LL * 24 * 60 * 60 * 1000);
        }
    } else {
        result = DAI_ERR_PARSE;
    }

    platform->http.free_response(platform->http.ctx, resp);
    return result;
}
