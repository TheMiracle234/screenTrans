#pragma once
#include <atomic>
#include <algorithm>
#include "env_value.hpp"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <process.h>
#endif
namespace asr_trace {
    inline std::string escape(const std::string& text) {
        std::string out;
        for (unsigned char c : text) { if (c == '"' || c == '\\') out += '\\'; if (c < 32) { const char* hex = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; } else out += char(c); }
        return out;
    }
    inline bool valid(const std::vector<float>& pcm) { for (float f : pcm) if (!std::isfinite(f) || std::abs(f) > 1.0f) return false; return true; }
    // Disk errors disable tracing only. Bounded copy queue; the writer owns files.
    class Writer {
        struct Item { std::string tag, detail; uint64_t source, epoch, seq, start; std::vector<float> pcm; double time_ms; };
        bool enabled_ = false, stop_ = false;
        std::mutex mutex_; std::condition_variable ready_; std::deque<Item> items_;
        size_t queued_ = 0; 
        std::atomic<uint64_t> lost_{ 0 }; 
        std::thread worker_;
    public:
        explicit Writer(const char* label) {
            const auto dir = flexmeet::environment("FLEXMEET_ASR_TRACE_DIR"); if (dir.empty()) return;
            try {
                auto root = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(dir.c_str()))); std::filesystem::create_directories(root);
                auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                auto base = root / (std::string(label) + "-" + std::to_string(_getpid()) + "-" + std::to_string(stamp));
                enabled_ = true;
                worker_ = std::thread([this, base] {
                    auto index_path = base; index_path += ".jsonl"; auto audio_path = base; audio_path += ".f32";
                    std::ofstream index(index_path), audio(audio_path, std::ios::binary);
                    uint64_t offset = 0;
                    for (;;) {
                        Item item;
                        { std::unique_lock lock(mutex_); ready_.wait(lock, [&] {return stop_ || !items_.empty();}); if (items_.empty() && stop_) break; item = std::move(items_.front()); items_.pop_front(); queued_ -= item.pcm.size() * 4; }
                        double sum = 0, peak = 0; size_t clipped = 0;
                        for (float f : item.pcm) { sum += double(f) * f; peak = (std::max)(peak, double(std::abs(f))); clipped += std::abs(f) >= .999f; }
                        index.precision(15);
                        index << "{\"time_ms\":" << item.time_ms << ",\"detail\":\"" << escape(item.detail) << "\",\"tag\":\"" << escape(item.tag) << "\",\"source\":" << item.source << ",\"epoch\":" << item.epoch << ",\"seq\":" << item.seq << ",\"start\":" << item.start << ",\"count\":" << item.pcm.size() << ",\"offset\":" << offset << ",\"rms\":" << (item.pcm.empty() ? 0 : std::sqrt(sum / item.pcm.size())) << ",\"peak\":" << peak << ",\"clip\":" << clipped << ",\"dropped_records\":" << lost_.load() << "}\n";
                        audio.write(reinterpret_cast<const char*>(item.pcm.data()), item.pcm.size() * 4); offset += item.pcm.size() * 4; index.flush(); audio.flush();
                    }
                    index << "{\"tag\":\"trace-end\",\"dropped_records\":" << lost_.load() << "}\n";
                    });
            }
            catch (...) { enabled_ = false; }
        }
        bool enabled() const { return enabled_; }
        ~Writer() { if (worker_.joinable()) { { std::lock_guard lock(mutex_); stop_ = true; } ready_.notify_one(); worker_.join(); } }
        void record(std::string tag, uint64_t source, uint64_t epoch, uint64_t seq, uint64_t start, const std::vector<float>& pcm = {}, std::string detail = {}) noexcept {
            try {
                if (!enabled_) return;
                // The writer holds this mutex only while moving a queue item, never during I/O.
                std::lock_guard lock(mutex_);
                if (queued_ + pcm.size() * 4 > 4 * 1024 * 1024 || items_.size() >= 2048) { ++lost_; return; }
                queued_ += pcm.size() * 4; items_.push_back({ std::move(tag),std::move(detail),source,epoch,seq,start,pcm,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count() }); ready_.notify_one();
            }
            catch (...) { ++lost_; }
        }
    };
}