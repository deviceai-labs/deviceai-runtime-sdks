/**
 * dai_backend.h — DeviceAI control-plane API client.
 *
 * Handles the three device-facing endpoints:
 *   POST /v1/devices/register — initial device registration (API key auth)
 *   GET  /v1/manifest         — model assignment manifest (device JWT auth)
 *   POST /v1/telemetry/batch  — telemetry ingest (device JWT auth)
 *   POST /v1/devices/refresh  — silent token refresh (device JWT auth)
 *
 * HTTP is executed via the platform-injected dai_http_executor_t — no libcurl.
 * All functions are synchronous; the caller is responsible for threading.
 */
#pragma once

#include <stdint.h>
#include "dai_platform.h"
#include "dai_session.h"
#include "dai_telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    DAI_OK                  =  0,
    DAI_ERR_NETWORK         = -1,  /**< Transport-level failure (no response) */
    DAI_ERR_HTTP            = -2,  /**< Non-2xx HTTP status */
    DAI_ERR_PARSE           = -3,  /**< Response JSON could not be parsed */
    DAI_ERR_UNAUTHORIZED    = -4,  /**< 401 — invalid or expired API key / token */
    DAI_ERR_INVALID_ARG     = -5,  /**< NULL or malformed input argument */
} dai_error_t;

/* ── Manifest types ────────────────────────────────────────────────────────── */

#define DAI_MANIFEST_MODEL_ID_LEN   128
#define DAI_MANIFEST_URL_LEN        512
#define DAI_MANIFEST_CHECKSUM_LEN   64
#define DAI_MANIFEST_FORMAT_LEN     32
#define DAI_MANIFEST_MAX_ENTRIES    64

typedef struct {
    char    model_id[DAI_MANIFEST_MODEL_ID_LEN];
    char    download_url[DAI_MANIFEST_URL_LEN];
    char    checksum_sha256[DAI_MANIFEST_CHECKSUM_LEN];
    int64_t size_bytes;
    char    format[DAI_MANIFEST_FORMAT_LEN];        /**< "gguf", "onnx", etc. */
    int     is_required;
    int     kill_switch;
    char    min_app_version[32];
} dai_manifest_entry_t;

typedef struct {
    dai_manifest_entry_t entries[DAI_MANIFEST_MAX_ENTRIES];
    int                  entry_count;
    char                 rollout_id[64];
    int64_t              fetched_at_ms;
} dai_manifest_t;

/* ── Capability profile ────────────────────────────────────────────────────── */

/** A single key/value pair in the capability profile sent at registration. */
typedef struct {
    const char* key;
    const char* value_json; /**< JSON-encoded value: "8.0", "true", "\"android\"", etc. */
} dai_capability_kv_t;

/* ── Client functions ──────────────────────────────────────────────────────── */

/**
 * Register this device with the DeviceAI control plane.
 *
 * Sends POST /v1/devices/register with the capability profile and returns a
 * DeviceSession containing device_id, a 30-day JWT, and a capability tier.
 *
 * @param base_url      Backend base URL (null-terminated, e.g. "https://api.deviceai.dev").
 * @param api_key       API key (null-terminated, e.g. "dai_live_...").
 * @param profile       Array of capability key/value pairs.
 * @param profile_count Number of entries in profile.
 * @param platform      Platform callbacks (http + clock + log).
 * @param out_session   Populated on DAI_OK.
 * @return              DAI_OK on success, negative error code on failure.
 */
dai_error_t dai_backend_register_device(
    const char*               base_url,
    const char*               api_key,
    const dai_capability_kv_t* profile,
    int                       profile_count,
    const dai_platform_t*     platform,
    dai_session_t*            out_session
);

/**
 * Fetch the model manifest for this device's cohort.
 *
 * @param base_url      Backend base URL.
 * @param device_token  JWT from dai_session_t.token.
 * @param platform      Platform callbacks.
 * @param out_manifest  Populated on DAI_OK.
 * @return              DAI_OK on success, negative error code on failure.
 */
dai_error_t dai_backend_fetch_manifest(
    const char*           base_url,
    const char*           device_token,
    const dai_platform_t* platform,
    dai_manifest_t*       out_manifest
);

/**
 * Send a batch of telemetry events to the backend.
 *
 * Fire-and-forget semantics — logs a warning on failure but does not propagate it.
 * The caller (TelemetryEngine) handles retries via sendWithBackoff.
 * Max 500 events per batch (backend limit); excess events are silently truncated.
 *
 * @param base_url      Backend base URL.
 * @param device_token  JWT from dai_session_t.token.
 * @param session_id    Client-generated UUID stable for the app session lifetime.
 * @param events        Array of events to send.
 * @param event_count   Number of events (capped at 500 internally).
 * @param platform      Platform callbacks.
 * @return              DAI_OK on success, negative error code on failure.
 */
dai_error_t dai_backend_ingest_telemetry(
    const char*           base_url,
    const char*           device_token,
    const char*           session_id,
    const dai_event_t*    events,
    int                   event_count,
    const dai_platform_t* platform
);

/**
 * Silently refresh the device token when dai_session_needs_refresh() returns true.
 *
 * @param base_url      Backend base URL.
 * @param session       Current session (token used for auth).
 * @param platform      Platform callbacks.
 * @param out_session   Updated session with new token on DAI_OK.
 * @return              DAI_OK on success, negative error code on failure.
 */
dai_error_t dai_backend_refresh_token(
    const char*           base_url,
    const dai_session_t*  session,
    const dai_platform_t* platform,
    dai_session_t*        out_session
);

#ifdef __cplusplus
}
#endif
