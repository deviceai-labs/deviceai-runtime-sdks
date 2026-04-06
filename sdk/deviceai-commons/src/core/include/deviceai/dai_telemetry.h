/**
 * dai_telemetry.h — On-device telemetry buffering and network-aware delivery.
 *
 * The TelemetryEngine maintains three separate ring buffers (normal, wifi-preferred,
 * critical) and delivers them according to their priority and the current network state
 * via the platform HTTP executor injected at init time.
 *
 * ## Delivery semantics
 * - Normal        — batched up to FLUSH_THRESHOLD events, sent on any network,
 *                   exponential backoff (1s / 2s / 4s, max 3 retries).
 * - WifiPreferred — deferred until the platform reports Wi-Fi. Falls back to Normal
 *                   if no isOnWifi checker is provided.
 * - Critical      — sent immediately on any network, bypasses batching and data-saver.
 *                   Never dropped. ControlPlaneAlert events always use this priority.
 *
 * ## Data Saver
 * When isDataSaver returns true, the Normal flush threshold is multiplied by
 * dataSaverMultiplier (default 5) — fewer, larger batches on constrained networks.
 *
 * ## What is collected (and what is not)
 * Collected: model_id, module, latency_ms, ttft_ms, tokens_per_sec, token_counts,
 *            input_length_ms (STT), output_chars (TTS), finish_reason, error_code.
 * Never collected: prompt/response text, audio data, transcripts, user PII.
 */
#pragma once

#include <stdint.h>
#include "dai_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Enums ─────────────────────────────────────────────────────────────────── */

typedef enum {
    DAI_TELEMETRY_OFF     = 0,
    DAI_TELEMETRY_MINIMAL = 1, /**< ModelLoad/Unload + InferenceComplete + ControlPlaneAlert */
    DAI_TELEMETRY_FULL    = 2, /**< All events including OtaDownload and ManifestSync */
} dai_telemetry_level_t;

typedef enum {
    DAI_PRIORITY_NORMAL         = 0,
    DAI_PRIORITY_WIFI_PREFERRED = 1,
    DAI_PRIORITY_CRITICAL       = 2,
} dai_priority_t;

typedef enum {
    DAI_EVENT_MODEL_LOAD          = 0,
    DAI_EVENT_MODEL_UNLOAD        = 1,
    DAI_EVENT_INFERENCE_COMPLETE  = 2,
    DAI_EVENT_OTA_DOWNLOAD        = 3,
    DAI_EVENT_MANIFEST_SYNC       = 4,
    DAI_EVENT_CONTROL_PLANE_ALERT = 5,
} dai_event_type_t;

/* ── Event structs ─────────────────────────────────────────────────────────── */

typedef struct {
    char    module[16];     /**< "llm", "stt", "tts" */
    char    model_id[128];
    int64_t duration_ms;
    float   ram_delta_mb;
    int     has_ram_delta;  /**< 1 if ram_delta_mb is valid */
} dai_event_model_load_t;

typedef struct {
    char    module[16];
    char    model_id[128];
} dai_event_model_unload_t;

typedef struct {
    char    module[16];
    char    model_id[128];
    int64_t latency_ms;
    int64_t ttft_ms;            int has_ttft;
    float   tokens_per_sec;     int has_tokens_per_sec;
    int     input_token_count;  int has_input_tokens;
    int     output_token_count; int has_output_tokens;
    int     input_length_ms;    int has_input_length_ms;  /**< STT: audio duration */
    int     output_chars;       int has_output_chars;     /**< TTS: chars synthesized */
    char    finish_reason[32];  /**< "stop", "max_tokens", "cancel", "error" */
} dai_event_inference_t;

typedef struct {
    char    model_id[128];
    char    version[32];
    int64_t size_bytes;
    int64_t duration_ms;
    int     success;
    char    error_code[64];
} dai_event_ota_download_t;

typedef struct {
    int     success;
    int     model_count;
    char    error_code[64];
} dai_event_manifest_sync_t;

typedef struct {
    char    alert_type[32];     /**< "kill_switch", "forced_rollback", "model_revoked" */
    char    model_id[128];
    char    rollout_id[64];
} dai_event_control_plane_alert_t;

/** Tagged union for all event types. */
typedef struct {
    dai_event_type_t type;
    int64_t          timestamp_ms;

    union {
        dai_event_model_load_t          model_load;
        dai_event_model_unload_t        model_unload;
        dai_event_inference_t           inference;
        dai_event_ota_download_t        ota_download;
        dai_event_manifest_sync_t       manifest_sync;
        dai_event_control_plane_alert_t control_plane_alert;
    };
} dai_event_t;

/* ── Network policy ────────────────────────────────────────────────────────── */

/**
 * Network-awareness callbacks. All are optional (NULL = no gating).
 * Called on the internal flush thread — must be thread-safe.
 */
typedef struct {
    /** Returns 1 if device is on Wi-Fi, 0 on cellular/unknown. NULL = treat as Wi-Fi. */
    int (*is_on_wifi)(void* ctx);

    /** Returns 1 if data saver / low-data mode is active. NULL = assume off. */
    int (*is_data_saver)(void* ctx);

    /**
     * When data saver is active, multiply the normal flush threshold by this factor.
     * Fewer, larger batches on constrained networks. Default: 5.
     */
    int data_saver_multiplier;

    void* ctx;
} dai_network_policy_t;

/* ── TelemetryEngine lifecycle ─────────────────────────────────────────────── */

/** Opaque engine handle. One per SDK instance. */
typedef struct dai_telemetry_engine_t dai_telemetry_engine_t;

/**
 * Create a TelemetryEngine.
 *
 * @param level      Verbosity — controls which event types are accepted.
 * @param policy     Network-awareness callbacks. May be zero-initialised for defaults.
 * @param base_url   DeviceAI backend base URL (null-terminated).
 * @param platform   Platform callbacks for HTTP, clock, and logging.
 * @return           Opaque engine handle. Must be destroyed via dai_telemetry_destroy().
 */
dai_telemetry_engine_t* dai_telemetry_create(
    dai_telemetry_level_t       level,
    const dai_network_policy_t* policy,
    const char*                 base_url,
    const dai_platform_t*       platform
);

/**
 * Record an event. Thread-safe. No-op if level is Off or event is filtered.
 * Critical events trigger an immediate async flush.
 */
void dai_telemetry_record(dai_telemetry_engine_t* engine, const dai_event_t* event);

/**
 * Flush all buffers that are ready given current network state.
 * Blocks until the flush attempt completes (or fails after backoff).
 * Call on Wi-Fi connect or before app backgrounding.
 */
void dai_telemetry_flush(dai_telemetry_engine_t* engine);

/**
 * Best-effort final flush then destroy. Blocks up to ~10s for in-flight sends.
 * After this call the handle is invalid.
 */
void dai_telemetry_destroy(dai_telemetry_engine_t* engine);

/**
 * Attach a device token so the engine can send authenticated batches.
 * Call after dai_backend_register_device() succeeds.
 */
void dai_telemetry_set_session(
    dai_telemetry_engine_t* engine,
    const char*             device_token,
    const char*             session_id
);

#ifdef __cplusplus
}
#endif
