/**
 * dai_platform.h — Platform injection callbacks for deviceai-commons.
 *
 * Each platform SDK (Kotlin/JNI, Swift, Flutter/FFI, React Native/JSI) implements
 * these callbacks and injects them via dai_core_init(). The C++ layer uses them for
 * HTTP, file storage, time, and logging — no libcurl, no platform SDKs in C++.
 *
 * Lifetime: all callbacks must remain valid for the lifetime of the SDK session.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── HTTP executor ─────────────────────────────────────────────────────────── */

/**
 * Synchronous HTTP response. The platform allocates this; the C++ layer calls
 * dai_http_free_response() when done. Keeps ownership clear across the boundary.
 */
typedef struct {
    int     status_code;
    char*   body;       /**< Null-terminated JSON body, or NULL on network error. */
    size_t  body_len;
    char*   error_msg;  /**< Set on transport failure; NULL on success. */
} dai_http_response_t;

/**
 * HTTP header key/value pair.
 */
typedef struct {
    const char* key;
    const char* value;
} dai_http_header_t;

/**
 * Platform HTTP executor. The C++ layer builds the request; the platform executes it.
 *
 * All calls are synchronous from C++'s perspective. The platform layer is responsible
 * for running these on a background thread if needed (Kotlin: runBlocking/Dispatchers.IO,
 * Swift: DispatchQueue.global(), Dart: Isolate).
 */
typedef struct {
    /**
     * Execute a POST request.
     *
     * @param ctx       Opaque platform context (e.g. OkHttpClient*, NSURLSession*).
     * @param url       Null-terminated URL string.
     * @param body      Null-terminated JSON body. NULL for empty body.
     * @param headers   Array of key/value header pairs.
     * @param n_headers Number of headers.
     * @return          Allocated response. Caller must pass to dai_http_free_response.
     */
    dai_http_response_t* (*post)(
        void*                   ctx,
        const char*             url,
        const char*             body,
        const dai_http_header_t* headers,
        int                     n_headers
    );

    /**
     * Execute a GET request.
     */
    dai_http_response_t* (*get)(
        void*                   ctx,
        const char*             url,
        const dai_http_header_t* headers,
        int                     n_headers
    );

    /**
     * Free a response returned by post() or get().
     */
    void (*free_response)(void* ctx, dai_http_response_t* response);

    void* ctx; /**< Passed as first arg to every callback. */
} dai_http_executor_t;

/* ── Storage ───────────────────────────────────────────────────────────────── */

/**
 * Platform storage callbacks. Used by SessionStore to persist DeviceSession.
 * All paths are absolute. Strings returned by the platform must be freed via free_string().
 */
typedef struct {
    /** Read file contents. Returns allocated null-terminated string, or NULL if not found. */
    char*   (*read_file)(void* ctx, const char* path);

    /** Write null-terminated content to path. Returns 1 on success, 0 on failure. */
    int     (*write_file)(void* ctx, const char* path, const char* content);

    /** Delete file at path. Returns 1 on success, 0 on failure / not found. */
    int     (*delete_file)(void* ctx, const char* path);

    /**
     * Returns the base directory for DeviceAI data files (models dir).
     * Returned string must be freed via free_string().
     */
    char*   (*models_dir)(void* ctx);

    /** Free a string previously returned by read_file() or models_dir(). */
    void    (*free_string)(void* ctx, char* str);

    void*   ctx;
} dai_storage_t;

/* ── Clock + Logging ───────────────────────────────────────────────────────── */

/** Returns current wall-clock time in milliseconds since Unix epoch. */
typedef int64_t (*dai_clock_fn)(void);

typedef enum {
    DAI_LOG_DEBUG = 0,
    DAI_LOG_INFO  = 1,
    DAI_LOG_WARN  = 2,
    DAI_LOG_ERROR = 3,
} dai_log_level_t;

/** Platform log sink. Forwards to Logcat, os_log, print, etc. */
typedef void (*dai_log_fn)(dai_log_level_t level, const char* tag, const char* message);

/* ── Aggregate platform config ─────────────────────────────────────────────── */

/**
 * All platform callbacks bundled together. Passed once to dai_core_init().
 * All function pointers must be non-NULL (except log_fn which falls back to stderr).
 */
typedef struct {
    dai_http_executor_t http;
    dai_storage_t       storage;
    dai_clock_fn        clock_ms;   /**< Current time in ms. */
    dai_log_fn          log;        /**< May be NULL — falls back to fprintf(stderr). */
} dai_platform_t;

#ifdef __cplusplus
}
#endif
