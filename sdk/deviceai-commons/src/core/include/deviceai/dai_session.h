/**
 * dai_session.h — DeviceSession: registered device identity and JWT.
 *
 * A session is created by dai_backend_register_device() and persisted locally
 * by dai_session_store_save(). On next cold start, dai_session_store_load()
 * restores it — the device skips re-registration if the token is still valid.
 */
#pragma once

#include <stdint.h>
#include "dai_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAI_SESSION_DEVICE_ID_LEN     64
#define DAI_SESSION_TOKEN_LEN         1024
#define DAI_SESSION_TIER_LEN          32

/** A registered device with a live JWT. */
typedef struct {
    char    device_id[DAI_SESSION_DEVICE_ID_LEN];
    char    token[DAI_SESSION_TOKEN_LEN];
    int64_t expires_at_ms;
    char    capability_tier[DAI_SESSION_TIER_LEN];
} dai_session_t;

/**
 * Returns 1 if the token has expired and the device must re-register.
 */
int dai_session_is_expired(const dai_session_t* session, dai_clock_fn clock_ms);

/**
 * Returns 1 if within 7 days of expiry — eligible for silent token refresh.
 */
int dai_session_needs_refresh(const dai_session_t* session, dai_clock_fn clock_ms);

/* ── SessionStore ──────────────────────────────────────────────────────────── */

/**
 * Persist session to storage. Non-fatal on failure — session is re-fetched on
 * next cold start.
 *
 * @param storage  Platform storage callbacks.
 * @param session  Session to persist.
 */
void dai_session_store_save(const dai_storage_t* storage, const dai_session_t* session);

/**
 * Load a previously persisted session from storage.
 *
 * @param storage   Platform storage callbacks.
 * @param out       Output session. Populated on success.
 * @return          1 on success, 0 if no session exists or it cannot be parsed.
 */
int dai_session_store_load(const dai_storage_t* storage, dai_session_t* out);

/**
 * Delete the persisted session (e.g. on sign-out or API key change).
 */
void dai_session_store_clear(const dai_storage_t* storage);

#ifdef __cplusplus
}
#endif
