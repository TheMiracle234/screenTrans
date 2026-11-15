#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#ifdef ASR_ENABLE_DIAGNOSTICS
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#endif

namespace asr {
    // Compiled out of the normal DLL. Diagnostic builds still require the env
    // switch, and never log PCM or transcript content. The observer does no inference.
    class Diagnostics {
    public:
#ifdef ASR_ENABLE_DIAGNOSTICS
        ~Diagnostics() { stop(); }
        void start(const char* dll, const char* models, int threads, const char* backend) {
            char flag[2]{}; size_t required = 0;
            if (getenv_s(&required, flag, sizeof(flag), "PLUGIN_ASR_DIAGNOSTICS") || std::string_view(flag) != "1") return;
            enabled_ = true;
            std::fprintf(stderr, "[ASR diagnostic] dll=%s models=%s threads=%d backend=%s\n", dll, models, threads, backend);
            observer_ = std::thread([this] {
                auto previous = std::chrono::steady_clock::now();
                uint64_t samples = 0;
                std::unique_lock lock(mutex_);
                while (!wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_; })) {
                    const auto now = std::chrono::steady_clock::now();
                    const double seconds = std::chrono::duration<double>(now - previous).count();
                    const auto total = samples_.load();
                    std::fprintf(stderr, "[ASR diagnostic] input_x=%.3f input_samples=%llu pushes=%llu pending_samples=%llu output_events=%llu polls=%llu completed=%llu cancelled=%llu gaps=%llu phase=%s decode_ms=%.1f last_begin_ms=%lld last_end_ms=%lld audio_samples=%llu dropped_samples=%llu ingress_samples=%llu ready_jobs=%llu inference_samples=%llu\n",
                        double(total - samples) / 16000 / seconds,
                        total, pushes_.load(), queue_.load(), output_.load(), polls_.load(), completed_.load(),
                        cancelled_.load(), gaps_.load(), phase_.load(), decode_ms_.load(), begin_.load(), end_.load(), audio_samples_.load(), dropped_samples_.load(), ingress_.load(), jobs_.load(), inflight_.load());
                    samples = total; previous = now;
                }
            });
        }
        void stop() {
            { std::lock_guard lock(mutex_); stopping_ = true; }
            wake_.notify_all();
            if (observer_.joinable()) observer_.join();
        }
        void input(uint32_t n) { samples_ += n; ++pushes_; }
        void initialization(double load, double state, double vad, double warm) {
            if (enabled_) std::fprintf(stderr, "[ASR initialization] models=1 states=1 load_ms=%.3f state_ms=%.3f vad_ms=%.3f warm_ms=%.3f\n", load, state, vad, warm);
        }
        void audio(size_t n) { audio_samples_ += n; }
        void dropped(uint64_t n) { dropped_samples_ += n; }
        void poll() { ++polls_; }
        void gap() { ++gaps_; }
        void queues(size_t input, size_t output) { queue_ = input; output_ = output; }
        void stages(size_t input, size_t jobs, size_t inflight) { ingress_ = input; jobs_ = jobs; inflight_ = inflight; }
        void phase(const char* text) { phase_ = text; }
        void decode_begin() { begin_ = timestamp(); phase("decode"); }
        void decode_end(double ms, const char* outcome, int64_t from, int64_t through, double queue, bool final, bool published) {
            end_ = timestamp(); decode_ms_ = ms;
            if (std::string_view(outcome) == "complete") ++completed_; else ++cancelled_;
            if (enabled_) std::fprintf(stderr, "[ASR decode] from=%lld through=%lld ms=%.3f queue_ms=%.3f outcome=%s kind=%s published=%d\n",
                static_cast<long long>(from), static_cast<long long>(through), ms, queue, outcome, final ? "final" : "partial", int(published));
            phase("idle");
        }
    private:
        static long long timestamp() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        std::atomic<unsigned long long> samples_{0}, pushes_{0}, polls_{0}, queue_{0}, output_{0}, completed_{0}, cancelled_{0}, gaps_{0};
        std::atomic<unsigned long long> audio_samples_{0}, dropped_samples_{0};
        std::atomic<unsigned long long> ingress_{0}, jobs_{0}, inflight_{0};
        bool enabled_ = false;
        std::atomic<const char*> phase_{"warmup"};
        std::atomic<double> decode_ms_{0};
        std::atomic<long long> begin_{0}, end_{0};
        std::mutex mutex_;
        std::condition_variable wake_;
        bool stopping_ = false;
        std::thread observer_;
#else
        void start(const char*, const char*, int, const char*) {}
        void stop() {}
        void input(uint32_t) {}
        void initialization(double, double, double, double) {}
        void audio(size_t) {}
        void dropped(uint64_t) {}
        void poll() {}
        void gap() {}
        void queues(size_t, size_t) {}
        void stages(size_t, size_t, size_t) {}
        void phase(const char*) {}
        void decode_begin() {}
        void decode_end(double, const char*, int64_t, int64_t, double, bool, bool) {}
#endif
    };
}
