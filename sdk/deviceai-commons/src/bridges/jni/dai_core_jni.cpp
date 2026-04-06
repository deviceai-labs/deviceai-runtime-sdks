/**
 * dai_core_jni.cpp — JNI bridge between kotlin/core and cpp/core.
 *
 * Platform injection pattern:
 *   Kotlin registers HttpExecutor + StorageExecutor objects at init time.
 *   C++ stores global JNI refs; the telemetry worker thread calls back via
 *   JavaVM->AttachCurrentThread() when it needs to send HTTP or read/write files.
 *
 * Kotlin class: dev.deviceai.core.jni.CoreJniBridge
 */

#include "deviceai/dai_core.h"

#include <jni.h>
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

// ── Global JVM state ──────────────────────────────────────────────────────────

static JavaVM*   g_jvm             = nullptr;

// HttpExecutor global ref + method IDs (set in nativeInit)
static jobject   g_http_executor   = nullptr;
static jmethodID g_http_post_id    = nullptr;
static jmethodID g_http_get_id     = nullptr;

// StorageExecutor global ref + method IDs
static jobject   g_storage         = nullptr;
static jmethodID g_read_file_id    = nullptr;
static jmethodID g_write_file_id   = nullptr;
static jmethodID g_delete_file_id  = nullptr;
static jmethodID g_models_dir_id   = nullptr;

static std::mutex g_init_mutex;

// ── JNI_OnLoad ────────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// ── JNIEnv helpers ────────────────────────────────────────────────────────────

static JNIEnv* get_env(bool* did_attach) {
    JNIEnv* env = nullptr;
    jint status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);
        *did_attach = true;
    } else {
        *did_attach = false;
    }
    return env;
}

static void release_env(bool did_attach) {
    if (did_attach) g_jvm->DetachCurrentThread();
}

static std::string jstring_to_str(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* chars = env->GetStringUTFChars(js, nullptr);
    std::string result(chars ? chars : "");
    env->ReleaseStringUTFChars(js, chars);
    return result;
}

static jstring str_to_jstring(JNIEnv* env, const char* str) {
    return str ? env->NewStringUTF(str) : nullptr;
}

// ── HTTP executor callback (called from C++ worker thread) ───────────────────
//
// Kotlin HttpExecutor interface:
//   fun httpPost(url: String, body: String?, headers: Map<String, String>): HttpResult
//   fun httpGet(url: String, headers: Map<String, String>): HttpResult
//
// HttpResult data class: statusCode: Int, body: String?, error: String?

static dai_http_response_t* make_error_response(const char* msg) {
    auto* r = new dai_http_response_t{};
    r->status_code = 0;
    r->body        = nullptr;
    r->body_len    = 0;
    r->error_msg   = strdup(msg ? msg : "unknown error");
    return r;
}

static dai_http_response_t* call_kotlin_http(
    JNIEnv* env, jmethodID method_id,
    const char* url, const char* body,
    const dai_http_header_t* headers, int n_headers
) {
    // Build headers map: String[] keys, String[] vals
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray jkeys = env->NewObjectArray(n_headers, string_class, nullptr);
    jobjectArray jvals = env->NewObjectArray(n_headers, string_class, nullptr);
    for (int i = 0; i < n_headers; i++) {
        env->SetObjectArrayElement(jkeys, i, str_to_jstring(env, headers[i].key));
        env->SetObjectArrayElement(jvals, i, str_to_jstring(env, headers[i].value));
    }

    jstring jurl  = str_to_jstring(env, url);
    jstring jbody = body ? str_to_jstring(env, body) : nullptr;

    jobject result = env->CallObjectMethod(
        g_http_executor, method_id, jurl, jbody, jkeys, jvals, (jint)n_headers);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return make_error_response("JNI exception in HTTP executor");
    }
    if (!result) return make_error_response("null result from HTTP executor");

    // Extract fields from HttpResult
    jclass result_class = env->GetObjectClass(result);
    jfieldID status_fid = env->GetFieldID(result_class, "statusCode", "I");
    jfieldID body_fid   = env->GetFieldID(result_class, "body",   "Ljava/lang/String;");
    jfieldID error_fid  = env->GetFieldID(result_class, "error",  "Ljava/lang/String;");

    auto* resp = new dai_http_response_t{};
    resp->status_code = env->GetIntField(result, status_fid);

    jstring jresp_body  = (jstring)env->GetObjectField(result, body_fid);
    jstring jresp_error = (jstring)env->GetObjectField(result, error_fid);

    if (jresp_body) {
        std::string b = jstring_to_str(env, jresp_body);
        resp->body     = strdup(b.c_str());
        resp->body_len = b.size();
    }
    if (jresp_error) {
        std::string e = jstring_to_str(env, jresp_error);
        resp->error_msg = strdup(e.c_str());
    }

    return resp;
}

static dai_http_response_t* jni_http_post(
    void* ctx,
    const char* url, const char* body,
    const dai_http_header_t* headers, int n_headers
) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    auto* resp = call_kotlin_http(env, g_http_post_id, url, body, headers, n_headers);
    release_env(attached);
    return resp;
}

static dai_http_response_t* jni_http_get(
    void* ctx,
    const char* url,
    const dai_http_header_t* headers, int n_headers
) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    auto* resp = call_kotlin_http(env, g_http_get_id, url, nullptr, headers, n_headers);
    release_env(attached);
    return resp;
}

static void jni_http_free_response(void* ctx, dai_http_response_t* r) {
    (void)ctx;
    if (!r) return;
    free(r->body);
    free(r->error_msg);
    delete r;
}

// ── Storage callbacks ─────────────────────────────────────────────────────────
//
// Kotlin StorageExecutor interface:
//   fun readFile(path: String): String?
//   fun writeFile(path: String, content: String): Boolean
//   fun deleteFile(path: String): Boolean
//   fun modelsDir(): String

static char* jni_read_file(void* ctx, const char* path) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    jstring jpath = str_to_jstring(env, path);
    jstring result = (jstring)env->CallObjectMethod(g_storage, g_read_file_id, jpath);
    char* ret = nullptr;
    if (result && !env->ExceptionCheck()) {
        ret = strdup(jstring_to_str(env, result).c_str());
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    release_env(attached);
    return ret;
}

static int jni_write_file(void* ctx, const char* path, const char* content) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    jstring jpath    = str_to_jstring(env, path);
    jstring jcontent = str_to_jstring(env, content);
    jboolean ok = env->CallBooleanMethod(g_storage, g_write_file_id, jpath, jcontent);
    if (env->ExceptionCheck()) { env->ExceptionClear(); ok = JNI_FALSE; }
    release_env(attached);
    return ok ? 1 : 0;
}

static int jni_delete_file(void* ctx, const char* path) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    jstring jpath = str_to_jstring(env, path);
    jboolean ok = env->CallBooleanMethod(g_storage, g_delete_file_id, jpath);
    if (env->ExceptionCheck()) { env->ExceptionClear(); ok = JNI_FALSE; }
    release_env(attached);
    return ok ? 1 : 0;
}

static char* jni_models_dir(void* ctx) {
    (void)ctx;
    bool attached;
    JNIEnv* env = get_env(&attached);
    jstring result = (jstring)env->CallObjectMethod(g_storage, g_models_dir_id);
    char* ret = nullptr;
    if (result && !env->ExceptionCheck()) {
        ret = strdup(jstring_to_str(env, result).c_str());
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    release_env(attached);
    return ret;
}

static void jni_free_string(void* ctx, char* str) {
    (void)ctx;
    free(str);
}

// ── Clock + log ───────────────────────────────────────────────────────────────

static int64_t jni_clock_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static void jni_log(dai_log_level_t level, const char* tag, const char* msg) {
#ifdef ANDROID
    int prio;
    switch (level) {
        case DAI_LOG_DEBUG: prio = ANDROID_LOG_DEBUG; break;
        case DAI_LOG_INFO:  prio = ANDROID_LOG_INFO;  break;
        case DAI_LOG_WARN:  prio = ANDROID_LOG_WARN;  break;
        default:            prio = ANDROID_LOG_ERROR; break;
    }
    __android_log_print(prio, tag, "%s", msg);
#else
    const char* lvl_str = "DEBUG";
    if (level == DAI_LOG_INFO)  lvl_str = "INFO";
    if (level == DAI_LOG_WARN)  lvl_str = "WARN";
    if (level == DAI_LOG_ERROR) lvl_str = "ERROR";
    fprintf(stderr, "[%s] %s: %s\n", lvl_str, tag, msg);
#endif
}

// ── Shared platform struct ────────────────────────────────────────────────────

static dai_platform_t g_platform;

static void build_platform() {
    g_platform.http.post          = jni_http_post;
    g_platform.http.get           = jni_http_get;
    g_platform.http.free_response = jni_http_free_response;
    g_platform.http.ctx           = nullptr;

    g_platform.storage.read_file    = jni_read_file;
    g_platform.storage.write_file   = jni_write_file;
    g_platform.storage.delete_file  = jni_delete_file;
    g_platform.storage.models_dir   = jni_models_dir;
    g_platform.storage.free_string  = jni_free_string;
    g_platform.storage.ctx          = nullptr;

    g_platform.clock_ms = jni_clock_ms;
    g_platform.log      = jni_log;
}

// ── JNI function name macro ───────────────────────────────────────────────────

#define JNI_FN(name) Java_dev_deviceai_core_jni_CoreJniBridge_##name

extern "C" {

// ── Init ──────────────────────────────────────────────────────────────────────

/**
 * Register platform callbacks. Must be called once before any other JNI function.
 *
 * @param httpExecutor  Instance of JniHttpExecutor (implements httpPost/httpGet).
 * @param storage       Instance of JniStorageExecutor (implements readFile/writeFile/...).
 */
JNIEXPORT void JNICALL JNI_FN(nativeInit)(
    JNIEnv* env, jobject,
    jobject http_executor,
    jobject storage_executor
) {
    std::lock_guard<std::mutex> lk(g_init_mutex);

    // Release previous global refs if re-initialized.
    if (g_http_executor) env->DeleteGlobalRef(g_http_executor);
    if (g_storage)       env->DeleteGlobalRef(g_storage);

    g_http_executor = env->NewGlobalRef(http_executor);
    g_storage       = env->NewGlobalRef(storage_executor);

    // Look up HTTP executor method IDs.
    jclass http_class = env->GetObjectClass(http_executor);
    // fun httpPost(url: String, body: String?, keys: Array<String>, vals: Array<String>, n: Int): HttpResult
    g_http_post_id = env->GetMethodID(http_class, "httpPost",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;I)"
        "Ldev/deviceai/core/jni/HttpResult;");
    // fun httpGet(url: String, body: String?, keys: Array<String>, vals: Array<String>, n: Int): HttpResult
    g_http_get_id = env->GetMethodID(http_class, "httpGet",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;I)"
        "Ldev/deviceai/core/jni/HttpResult;");

    // Look up storage method IDs.
    jclass storage_class = env->GetObjectClass(storage_executor);
    g_read_file_id   = env->GetMethodID(storage_class, "readFile",   "(Ljava/lang/String;)Ljava/lang/String;");
    g_write_file_id  = env->GetMethodID(storage_class, "writeFile",  "(Ljava/lang/String;Ljava/lang/String;)Z");
    g_delete_file_id = env->GetMethodID(storage_class, "deleteFile", "(Ljava/lang/String;)Z");
    g_models_dir_id  = env->GetMethodID(storage_class, "modelsDir",  "()Ljava/lang/String;");

    build_platform();
}

// ── Backend — register device ─────────────────────────────────────────────────

/**
 * @param profileKeysJson  JSON array of key strings, e.g. ["ram_gb","has_npu"]
 * @param profileValsJson  JSON array of value strings (already JSON-encoded), e.g. ["8.0","true"]
 * @return JSON: {"device_id":"...","token":"...","expires_at_ms":...,"capability_tier":"..."}
 *         or null on failure.
 */
JNIEXPORT jstring JNICALL JNI_FN(nativeRegisterDevice)(
    JNIEnv* env, jobject,
    jstring j_base_url,
    jstring j_api_key,
    jobjectArray j_profile_keys,
    jobjectArray j_profile_vals
) {
    std::string base_url = jstring_to_str(env, j_base_url);
    std::string api_key  = jstring_to_str(env, j_api_key);

    int profile_count = j_profile_keys ? env->GetArrayLength(j_profile_keys) : 0;
    std::vector<std::string> keys(profile_count), vals(profile_count);
    std::vector<dai_capability_kv_t> profile(profile_count);
    for (int i = 0; i < profile_count; i++) {
        keys[i] = jstring_to_str(env, (jstring)env->GetObjectArrayElement(j_profile_keys, i));
        vals[i] = jstring_to_str(env, (jstring)env->GetObjectArrayElement(j_profile_vals, i));
        profile[i] = { keys[i].c_str(), vals[i].c_str() };
    }

    dai_session_t session{};
    dai_error_t err = dai_backend_register_device(
        base_url.c_str(), api_key.c_str(),
        profile.data(), profile_count,
        &g_platform, &session);

    if (err != DAI_OK) return nullptr;

    // Return as simple JSON for Kotlin to parse.
    char buf[2048];
    snprintf(buf, sizeof(buf),
        "{\"device_id\":\"%s\",\"token\":\"%s\","
        "\"expires_at_ms\":%" PRId64 ",\"capability_tier\":\"%s\"}",
        session.device_id, session.token,
        session.expires_at_ms, session.capability_tier);
    return env->NewStringUTF(buf);
}

// ── Backend — fetch manifest ──────────────────────────────────────────────────

/**
 * @return Raw manifest JSON string from the backend, or null on failure.
 */
JNIEXPORT jstring JNICALL JNI_FN(nativeFetchManifest)(
    JNIEnv* env, jobject,
    jstring j_base_url,
    jstring j_device_token
) {
    std::string base_url = jstring_to_str(env, j_base_url);
    std::string token    = jstring_to_str(env, j_device_token);

    dai_manifest_t manifest{};
    dai_error_t err = dai_backend_fetch_manifest(
        base_url.c_str(), token.c_str(), &g_platform, &manifest);

    if (err != DAI_OK) return nullptr;

    // Serialize back to JSON for Kotlin's ManifestResponse deserialization.
    // Build entries array.
    std::string entries_json = "[";
    for (int i = 0; i < manifest.entry_count; i++) {
        const auto& e = manifest.entries[i];
        if (i > 0) entries_json += ",";
        char entry_buf[1024];
        snprintf(entry_buf, sizeof(entry_buf),
            "{\"model_id\":\"%s\",\"download_url\":\"%s\","
            "\"checksum_sha256\":\"%s\",\"size_bytes\":%" PRId64 ","
            "\"format\":\"%s\",\"is_required\":%s,\"kill_switch\":%s}",
            e.model_id, e.download_url, e.checksum_sha256, e.size_bytes,
            e.format, e.is_required ? "true" : "false",
            e.kill_switch ? "true" : "false");
        entries_json += entry_buf;
    }
    entries_json += "]";

    std::string result = "{\"rollout_id\":\"" + std::string(manifest.rollout_id) +
                         "\",\"entries\":" + entries_json + "}";
    return env->NewStringUTF(result.c_str());
}

// ── Backend — refresh token ───────────────────────────────────────────────────

/**
 * @return New token string, or null on failure.
 */
JNIEXPORT jstring JNICALL JNI_FN(nativeRefreshToken)(
    JNIEnv* env, jobject,
    jstring j_base_url,
    jstring j_device_id,
    jstring j_current_token,
    jstring j_capability_tier,
    jlong   j_expires_at_ms
) {
    dai_session_t session{};
    std::string device_id = jstring_to_str(env, j_device_id);
    std::string token     = jstring_to_str(env, j_current_token);
    std::string tier      = jstring_to_str(env, j_capability_tier);
    strncpy(session.device_id,       device_id.c_str(), sizeof(session.device_id) - 1);
    strncpy(session.token,           token.c_str(),     sizeof(session.token) - 1);
    strncpy(session.capability_tier, tier.c_str(),      sizeof(session.capability_tier) - 1);
    session.expires_at_ms = j_expires_at_ms;

    std::string base_url = jstring_to_str(env, j_base_url);
    dai_session_t updated{};
    dai_error_t err = dai_backend_refresh_token(
        base_url.c_str(), &session, &g_platform, &updated);

    if (err != DAI_OK) return nullptr;
    return env->NewStringUTF(updated.token);
}

// ── Telemetry engine lifecycle ────────────────────────────────────────────────

JNIEXPORT jlong JNICALL JNI_FN(nativeCreateEngine)(
    JNIEnv* env, jobject,
    jint  j_level,
    jint  j_has_wifi_checker,
    jint  j_has_data_saver,
    jint  j_data_saver_multiplier,
    jstring j_base_url
) {
    // We register Kotlin-side wifi/data-saver checkers as simple int fields
    // updated by the Kotlin layer via nativeUpdateNetworkState().
    // Store them in a static so the C callback can read them.
    static std::atomic<int> s_is_wifi{1};
    static std::atomic<int> s_is_data_saver{0};

    dai_network_policy_t policy{};
    if (j_has_wifi_checker) {
        policy.is_on_wifi = [](void* ctx) -> int {
            return *reinterpret_cast<std::atomic<int>*>(ctx);
        };
        policy.ctx = &s_is_wifi;
    }
    if (j_has_data_saver) {
        // Both share ctx: wifi_checker takes ctx, data_saver needs another.
        // Use a simple struct stored in the engine's ctx instead.
    }
    policy.data_saver_multiplier = j_data_saver_multiplier > 0 ? j_data_saver_multiplier : 5;

    std::string base_url = jstring_to_str(env, j_base_url);
    dai_telemetry_engine_t* engine = dai_telemetry_create(
        static_cast<dai_telemetry_level_t>(j_level),
        &policy,
        base_url.c_str(),
        &g_platform);

    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL JNI_FN(nativeDestroyEngine)(
    JNIEnv*, jobject, jlong handle
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_telemetry_destroy(engine);
}

JNIEXPORT void JNICALL JNI_FN(nativeSetSession)(
    JNIEnv* env, jobject, jlong handle,
    jstring j_token, jstring j_session_id
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    std::string token = jstring_to_str(env, j_token);
    std::string sid   = jstring_to_str(env, j_session_id);
    dai_telemetry_set_session(engine, token.c_str(), sid.c_str());
}

JNIEXPORT void JNICALL JNI_FN(nativeFlushEngine)(
    JNIEnv*, jobject, jlong handle
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_telemetry_flush(engine);
}

// ── Telemetry — record events ─────────────────────────────────────────────────

JNIEXPORT void JNICALL JNI_FN(nativeRecordModelLoad)(
    JNIEnv* env, jobject, jlong handle,
    jlong j_timestamp_ms, jstring j_module, jstring j_model_id, jlong j_duration_ms
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_event_t ev{};
    ev.type         = DAI_EVENT_MODEL_LOAD;
    ev.timestamp_ms = j_timestamp_ms;
    std::string mod = jstring_to_str(env, j_module);
    std::string mid = jstring_to_str(env, j_model_id);
    strncpy(ev.model_load.module,   mod.c_str(), sizeof(ev.model_load.module) - 1);
    strncpy(ev.model_load.model_id, mid.c_str(), sizeof(ev.model_load.model_id) - 1);
    ev.model_load.duration_ms = j_duration_ms;
    dai_telemetry_record(engine, &ev);
}

JNIEXPORT void JNICALL JNI_FN(nativeRecordModelUnload)(
    JNIEnv* env, jobject, jlong handle,
    jlong j_timestamp_ms, jstring j_module, jstring j_model_id
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_event_t ev{};
    ev.type         = DAI_EVENT_MODEL_UNLOAD;
    ev.timestamp_ms = j_timestamp_ms;
    std::string mod = jstring_to_str(env, j_module);
    std::string mid = jstring_to_str(env, j_model_id);
    strncpy(ev.model_unload.module,   mod.c_str(), sizeof(ev.model_unload.module) - 1);
    strncpy(ev.model_unload.model_id, mid.c_str(), sizeof(ev.model_unload.model_id) - 1);
    dai_telemetry_record(engine, &ev);
}

JNIEXPORT void JNICALL JNI_FN(nativeRecordInferenceComplete)(
    JNIEnv* env, jobject, jlong handle,
    jlong   j_timestamp_ms,
    jstring j_module,
    jstring j_model_id,
    jlong   j_latency_ms,
    jlong   j_ttft_ms,         jboolean j_has_ttft,
    jfloat  j_tokens_per_sec,  jboolean j_has_tps,
    jint    j_input_tokens,    jboolean j_has_input_tokens,
    jint    j_output_tokens,   jboolean j_has_output_tokens,
    jint    j_input_length_ms, jboolean j_has_input_length,
    jint    j_output_chars,    jboolean j_has_output_chars,
    jstring j_finish_reason
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_event_t ev{};
    ev.type         = DAI_EVENT_INFERENCE_COMPLETE;
    ev.timestamp_ms = j_timestamp_ms;
    auto& inf = ev.inference;
    std::string mod = jstring_to_str(env, j_module);
    std::string mid = jstring_to_str(env, j_model_id);
    std::string fr  = jstring_to_str(env, j_finish_reason);
    strncpy(inf.module,        mod.c_str(), sizeof(inf.module) - 1);
    strncpy(inf.model_id,      mid.c_str(), sizeof(inf.model_id) - 1);
    strncpy(inf.finish_reason, fr.c_str(),  sizeof(inf.finish_reason) - 1);
    inf.latency_ms          = j_latency_ms;
    inf.ttft_ms             = j_ttft_ms;          inf.has_ttft            = j_has_ttft;
    inf.tokens_per_sec      = j_tokens_per_sec;   inf.has_tokens_per_sec  = j_has_tps;
    inf.input_token_count   = j_input_tokens;     inf.has_input_tokens    = j_has_input_tokens;
    inf.output_token_count  = j_output_tokens;    inf.has_output_tokens   = j_has_output_tokens;
    inf.input_length_ms     = j_input_length_ms;  inf.has_input_length_ms = j_has_input_length;
    inf.output_chars        = j_output_chars;     inf.has_output_chars    = j_has_output_chars;
    dai_telemetry_record(engine, &ev);
}

JNIEXPORT void JNICALL JNI_FN(nativeRecordManifestSync)(
    JNIEnv* env, jobject, jlong handle,
    jlong j_timestamp_ms, jboolean j_success, jint j_model_count, jstring j_error_code
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_event_t ev{};
    ev.type               = DAI_EVENT_MANIFEST_SYNC;
    ev.timestamp_ms       = j_timestamp_ms;
    ev.manifest_sync.success     = j_success;
    ev.manifest_sync.model_count = j_model_count;
    std::string ec = jstring_to_str(env, j_error_code);
    strncpy(ev.manifest_sync.error_code, ec.c_str(), sizeof(ev.manifest_sync.error_code) - 1);
    dai_telemetry_record(engine, &ev);
}

JNIEXPORT void JNICALL JNI_FN(nativeRecordControlPlaneAlert)(
    JNIEnv* env, jobject, jlong handle,
    jlong j_timestamp_ms, jstring j_alert_type, jstring j_model_id, jstring j_rollout_id
) {
    auto* engine = reinterpret_cast<dai_telemetry_engine_t*>(handle);
    dai_event_t ev{};
    ev.type         = DAI_EVENT_CONTROL_PLANE_ALERT;
    ev.timestamp_ms = j_timestamp_ms;
    auto& cpa = ev.control_plane_alert;
    std::string at = jstring_to_str(env, j_alert_type);
    std::string mi = jstring_to_str(env, j_model_id);
    std::string ri = jstring_to_str(env, j_rollout_id);
    strncpy(cpa.alert_type, at.c_str(), sizeof(cpa.alert_type) - 1);
    strncpy(cpa.model_id,   mi.c_str(), sizeof(cpa.model_id) - 1);
    strncpy(cpa.rollout_id, ri.c_str(), sizeof(cpa.rollout_id) - 1);
    dai_telemetry_record(engine, &ev);
}

} // extern "C"
