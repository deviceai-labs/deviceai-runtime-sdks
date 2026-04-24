#include "deviceai/dai_telemetry.h"
#include "deviceai/dai_backend.h"
#include "json_builder.h"

#include <deque>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <chrono>

#define BUFFER_CAPACITY           256
#define FLUSH_THRESHOLD           100
#define WIFI_FLUSH_THRESHOLD       50
#define CRITICAL_BUFFER_CAPACITY   32
#define MAX_RETRIES                 3
#define BACKOFF_BASE_MS          1000L
#define BACKOFF_MAX_MS          30000L

/* ── Priority resolution ───────────────────────────────────────────────────── */

static dai_priority_t event_priority(const dai_event_t* ev) {
    switch (ev->type) {
        case DAI_EVENT_CONTROL_PLANE_ALERT: return DAI_PRIORITY_CRITICAL;
        case DAI_EVENT_OTA_DOWNLOAD:
        case DAI_EVENT_MANIFEST_SYNC:       return DAI_PRIORITY_WIFI_PREFERRED;
        default:                            return DAI_PRIORITY_NORMAL;
    }
}

static bool should_record(dai_telemetry_level_t level, const dai_event_t* ev) {
    if (level == DAI_TELEMETRY_OFF) return false;
    if (level == DAI_TELEMETRY_FULL) return true;
    // Minimal: ModelLoad, ModelUnload, InferenceComplete, ControlPlaneAlert
    return ev->type == DAI_EVENT_MODEL_LOAD
        || ev->type == DAI_EVENT_MODEL_UNLOAD
        || ev->type == DAI_EVENT_INFERENCE_COMPLETE
        || ev->type == DAI_EVENT_CONTROL_PLANE_ALERT;
}

/* ── Engine struct ─────────────────────────────────────────────────────────── */

struct dai_telemetry_engine_t {
    dai_telemetry_level_t  level;
    dai_network_policy_t   policy;
    std::string            base_url;
    dai_platform_t         platform;
    int                    configured_flush_threshold;

    // Session — set after device registration
    std::mutex  session_mutex;
    std::string device_token;
    std::string session_id;
    bool        has_session = false;

    // Three-tier ring buffers
    std::mutex              normal_mutex;
    std::deque<dai_event_t> normal_buffer;

    std::mutex              wifi_mutex;
    std::deque<dai_event_t> wifi_buffer;

    std::mutex              critical_mutex;
    std::deque<dai_event_t> critical_buffer;

    // Flush worker
    std::thread             flush_thread;
    std::condition_variable flush_cv;
    std::mutex              flush_cv_mutex;
    std::atomic<bool>       flush_requested{false};
    std::atomic<bool>       shutting_down{false};

    void log(dai_log_level_t lvl, const char* msg) const {
        if (platform.log) platform.log(lvl, "TelemetryEngine", msg);
        else fprintf(stderr, "[TelemetryEngine] %s\n", msg);
    }

    int64_t now_ms() const {
        if (platform.clock_ms) return platform.clock_ms();
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    bool is_on_wifi() const {
        if (!policy.is_on_wifi) return true; // no checker = treat as any network
        return policy.is_on_wifi(policy.ctx) != 0;
    }

    bool is_data_saver() const {
        if (!policy.is_data_saver) return false;
        return policy.is_data_saver(policy.ctx) != 0;
    }

    int normal_flush_threshold() const {
        int base = configured_flush_threshold > 0 ? configured_flush_threshold : FLUSH_THRESHOLD;
        if (is_data_saver()) {
            int mult = policy.data_saver_multiplier > 0 ? policy.data_saver_multiplier : 5;
            return base * mult;
        }
        return base;
    }

    // ── Flush helpers ─────────────────────────────────────────────────────

    void send_with_backoff(std::vector<dai_event_t>& batch, const char* tag) {
        std::string token;
        std::string sid;
        {
            std::lock_guard<std::mutex> lk(session_mutex);
            if (!has_session) {
                log(DAI_LOG_DEBUG, "no session yet — deferring batch");
                return;
            }
            token = device_token;
            sid   = session_id;
        }

        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            dai_error_t err = dai_backend_ingest_telemetry(
                base_url.c_str(), token.c_str(), sid.c_str(),
                batch.data(), (int)batch.size(), &platform);

            if (err == DAI_OK) {
                char msg[128];
                snprintf(msg, sizeof(msg), "[%s] flushed %zu events", tag, batch.size());
                log(DAI_LOG_DEBUG, msg);
                return;
            }

            if (attempt < MAX_RETRIES) {
                long delay = std::min(BACKOFF_BASE_MS * (1L << (attempt - 1)), BACKOFF_MAX_MS);
                char msg[128];
                snprintf(msg, sizeof(msg), "[%s] attempt %d failed (err=%d), retry in %ldms",
                         tag, attempt, err, delay);
                log(DAI_LOG_DEBUG, msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "[%s] giving up after %d retries, re-queuing %zu events",
                 tag, MAX_RETRIES, batch.size());
        log(DAI_LOG_WARN, msg);

        // Re-queue to front of normal buffer (best-effort, non-critical only)
        if (strcmp(tag, "critical") != 0) {
            std::lock_guard<std::mutex> lk(normal_mutex);
            int space = BUFFER_CAPACITY - (int)normal_buffer.size();
            int requeue = std::min((int)batch.size(), space);
            for (int i = requeue - 1; i >= 0; i--) {
                normal_buffer.push_front(batch[i]);
            }
        }
    }

    void flush_critical() {
        std::vector<dai_event_t> batch;
        {
            std::lock_guard<std::mutex> lk(critical_mutex);
            if (critical_buffer.empty()) return;
            batch.assign(critical_buffer.begin(), critical_buffer.end());
            critical_buffer.clear();
        }
        send_with_backoff(batch, "critical");
    }

    void flush_wifi_preferred() {
        if (!is_on_wifi()) {
            log(DAI_LOG_DEBUG, "wifi-preferred flush skipped — not on Wi-Fi");
            return;
        }
        std::vector<dai_event_t> batch;
        {
            std::lock_guard<std::mutex> lk(wifi_mutex);
            if (wifi_buffer.empty()) return;
            batch.assign(wifi_buffer.begin(), wifi_buffer.end());
            wifi_buffer.clear();
        }
        send_with_backoff(batch, "wifi-preferred");
    }

    void flush_normal() {
        std::vector<dai_event_t> batch;
        {
            std::lock_guard<std::mutex> lk(normal_mutex);
            if (normal_buffer.empty()) return;
            batch.assign(normal_buffer.begin(), normal_buffer.end());
            normal_buffer.clear();
        }
        send_with_backoff(batch, "normal");
    }

    void flush_all() {
        flush_critical();
        flush_wifi_preferred();
        flush_normal();
    }

    // ── Worker thread ─────────────────────────────────────────────────────

    void run_flush_worker() {
        while (!shutting_down.load()) {
            std::unique_lock<std::mutex> lk(flush_cv_mutex);
            flush_cv.wait(lk, [this] {
                return flush_requested.load() || shutting_down.load();
            });
            flush_requested.store(false);
            lk.unlock();

            if (shutting_down.load()) break;
            flush_all();
        }
        // Final flush on shutdown
        flush_all();
    }
};

/* ── Public API ────────────────────────────────────────────────────────────── */

dai_telemetry_engine_t* dai_telemetry_create(
    dai_telemetry_level_t       level,
    const dai_network_policy_t* policy,
    const char*                 base_url,
    const dai_platform_t*       platform,
    int                         flush_threshold
) {
    if (!base_url || !platform) return nullptr;

    auto* engine = new dai_telemetry_engine_t();
    engine->level    = level;
    engine->base_url = base_url;
    engine->platform = *platform;
    engine->configured_flush_threshold = flush_threshold;

    if (policy) {
        engine->policy = *policy;
    } else {
        memset(&engine->policy, 0, sizeof(engine->policy));
        engine->policy.data_saver_multiplier = 5;
    }
    if (engine->policy.data_saver_multiplier <= 0)
        engine->policy.data_saver_multiplier = 5;

    engine->flush_thread = std::thread(&dai_telemetry_engine_t::run_flush_worker, engine);
    return engine;
}

void dai_telemetry_set_session(
    dai_telemetry_engine_t* engine,
    const char*             device_token,
    const char*             session_id
) {
    if (!engine || !device_token) return;
    std::lock_guard<std::mutex> lk(engine->session_mutex);
    engine->device_token = device_token;
    engine->session_id   = session_id ? session_id : "";
    engine->has_session  = true;
}

void dai_telemetry_record(dai_telemetry_engine_t* engine, const dai_event_t* event) {
    if (!engine || !event) return;
    if (!should_record(engine->level, event)) return;

    dai_priority_t priority = event_priority(event);

    // Effective priority: WifiPreferred degrades to Normal when no wifi checker
    if (priority == DAI_PRIORITY_WIFI_PREFERRED && !engine->policy.is_on_wifi) {
        priority = DAI_PRIORITY_NORMAL;
    }

    switch (priority) {
        case DAI_PRIORITY_CRITICAL: {
            {
                std::lock_guard<std::mutex> lk(engine->critical_mutex);
                if ((int)engine->critical_buffer.size() >= CRITICAL_BUFFER_CAPACITY)
                    engine->critical_buffer.pop_front();
                engine->critical_buffer.push_back(*event);
            }
            // Trigger immediate flush
            {
                std::lock_guard<std::mutex> lk(engine->flush_cv_mutex);
                engine->flush_requested.store(true);
            }
            engine->flush_cv.notify_one();
            break;
        }
        case DAI_PRIORITY_WIFI_PREFERRED: {
            int count;
            {
                std::lock_guard<std::mutex> lk(engine->wifi_mutex);
                if ((int)engine->wifi_buffer.size() >= BUFFER_CAPACITY)
                    engine->wifi_buffer.pop_front();
                engine->wifi_buffer.push_back(*event);
                count = (int)engine->wifi_buffer.size();
            }
            if (count >= WIFI_FLUSH_THRESHOLD && engine->is_on_wifi()) {
                std::lock_guard<std::mutex> lk(engine->flush_cv_mutex);
                engine->flush_requested.store(true);
                engine->flush_cv.notify_one();
            }
            break;
        }
        case DAI_PRIORITY_NORMAL: {
            int count;
            {
                std::lock_guard<std::mutex> lk(engine->normal_mutex);
                if ((int)engine->normal_buffer.size() >= BUFFER_CAPACITY)
                    engine->normal_buffer.pop_front();
                engine->normal_buffer.push_back(*event);
                count = (int)engine->normal_buffer.size();
            }
            if (count >= engine->normal_flush_threshold()) {
                std::lock_guard<std::mutex> lk(engine->flush_cv_mutex);
                engine->flush_requested.store(true);
                engine->flush_cv.notify_one();
            }
            break;
        }
    }
}

void dai_telemetry_flush(dai_telemetry_engine_t* engine) {
    if (!engine) return;
    engine->flush_all();
}

void dai_telemetry_destroy(dai_telemetry_engine_t* engine) {
    if (!engine) return;
    engine->shutting_down.store(true);
    engine->flush_cv.notify_one();
    if (engine->flush_thread.joinable()) engine->flush_thread.join();
    delete engine;
}
