/**
 * dai_core.h — Umbrella header for deviceai-commons.
 *
 * Platform SDKs (Kotlin/JNI, Swift, Flutter/FFI, React Native/JSI) include this
 * single header. It pulls in all public API surfaces:
 *
 *   dai_platform.h   — platform injection callbacks (HTTP, storage, clock, log)
 *   dai_session.h    — DeviceSession struct + SessionStore
 *   dai_telemetry.h  — TelemetryEngine (event recording, buffering, delivery)
 *   dai_backend.h    — control-plane API client (register, manifest, telemetry batch)
 *
 * ## Integration pattern
 *
 * 1. Build platform callbacks:
 *      dai_platform_t platform = { .http = my_http_executor, .storage = my_storage,
 *                                  .clock_ms = my_clock, .log = my_log };
 *
 * 2. Bootstrap (managed mode):
 *      dai_session_t session;
 *      if (!dai_session_store_load(&platform.storage, &session) ||
 *          dai_session_is_expired(&session, platform.clock_ms)) {
 *          dai_backend_register_device(base_url, api_key, profile, n, &platform, &session);
 *          dai_session_store_save(&platform.storage, &session);
 *      }
 *
 * 3. Create telemetry engine:
 *      dai_telemetry_engine_t* engine = dai_telemetry_create(
 *          DAI_TELEMETRY_MINIMAL, &policy, base_url, &platform);
 *      dai_telemetry_set_session(engine, session.token, session_id);
 *
 * 4. Record events from any SDK module:
 *      dai_event_t evt = { .type = DAI_EVENT_INFERENCE_COMPLETE, ... };
 *      dai_telemetry_record(engine, &evt);
 *
 * 5. Shutdown:
 *      dai_telemetry_destroy(engine);  // final flush then free
 */
#pragma once

#include "dai_platform.h"
#include "dai_session.h"
#include "dai_telemetry.h"
#include "dai_backend.h"

#define DAI_COMMONS_VERSION "0.2.0-alpha02"
