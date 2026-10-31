#pragma once
#include "plugin/asr.h"
#include "stream_engine.hpp"
#include "audio_quality.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace asr {
    using Clock = std::chrono::steady_clock;
    inline double milliseconds(Clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    }
    // Shared by the real backend and controlled regression backends. Only the
    // worker touches Engine/VAD/filter state; producer locks never cover inference.
    class Worker : public Backend {
    public:
        ~Worker() override { stop(); }
        void start() { worker_ = std::thread([this] { run(); }); }
        // Derived destructor MUST stop before releasing its backend resources.
        void stop() {
            stopping_.store(true); generation_.fetch_add(1); ready_.notify_all();
            if (worker_.joinable()) worker_.join();
            drained_cv_.notify_all();
        }
        bool cancelled() const override {
            return stopping_.load() || failed_.load() || generation_.load() != working_generation_;
        }
        bool allow_partial() override {
            std::lock_guard lock(mutex_);
            return queued_ < 400 * 16 && !closed_;
        }
        int32_t push(const float* pcm, uint32_t count) {
            if ((!pcm && count) || count > PLUGIN_ASR_MAX_SAMPLES) return PLUGIN_ASR_INVALID;
            for (uint32_t i = 0; i < count; ++i)
                if (!std::isfinite(pcm[i]) || std::abs(pcm[i]) > 1.f) return PLUGIN_ASR_INVALID;
            std::lock_guard lock(mutex_);
            if (closed_ || failed_ || stopping_) return PLUGIN_ASR_CLOSED;
            if (!count) return PLUGIN_ASR_OK;
            // Allocate before changing stream state, so allocation failure cannot
            // claim a discontinuity without accepting the replacement audio.
            Chunk chunk{ std::vector<float>(pcm, pcm + count), Clock::now() };
            int32_t result = PLUGIN_ASR_OK;
            if (queued_ + count > PLUGIN_ASR_MAX_SAMPLES) {
                generation_.fetch_add(1); reset_pending_ = true;
                reset_position_ = next_sample_;
                input_.clear(); queued_ = 0;
                erase_drafts_locked(0, true);
                Event event; event.kind = Event::gap; event.start = reset_position_;
                event.text = "ASR audio queue overflow; stale audio cancelled";
                if (!enqueue_locked(std::move(event))) return PLUGIN_ASR_CLOSED;
                result = PLUGIN_ASR_OVERFLOW;
            }
            input_.push_back(std::move(chunk)); queued_ += count; next_sample_ += count;
            ready_.notify_one(); return result;
        }
        int32_t poll(plugin_asr_result* out, char* text, uint32_t capacity) {
            if (!out || out->struct_size != sizeof(*out) || (!text && capacity)) return PLUGIN_ASR_INVALID;
            std::lock_guard lock(mutex_);
            *out = {}; out->struct_size = sizeof(*out);
            if (output_.empty()) return PLUGIN_ASR_EMPTY;
            const auto& e = output_.front();
            out->type = e.kind; out->utterance_id = e.id; out->revision = e.revision;
            out->start_sample = e.start; out->end_sample = e.end;
            out->inference_ms = e.inference_ms; out->queue_ms = e.queue_ms;
            out->text_bytes = static_cast<uint32_t>(e.text.size() + 1);
            if (!text || capacity < out->text_bytes) {
                pinned_ = true; return PLUGIN_ASR_BUFFER_SMALL;
            }
            std::memcpy(text, e.text.c_str(), out->text_bytes);
            output_.pop_front(); pinned_ = false; return PLUGIN_ASR_EVENT;
        }
        int32_t finish() {
            std::unique_lock lock(mutex_);
            if (failed_ || stopping_) return PLUGIN_ASR_CLOSED;

            if (!closed_) {
                closed_ = true;
                finish_pending_ = true;
                ready_.notify_one();
            }

            // 同步等待后台线程把最后的数据推理完并放入 output_ 队列
            drained_cv_.wait(lock, [&] { return finish_done_ || failed_ || stopping_; });

            return failed_ ? PLUGIN_ASR_FAILED : PLUGIN_ASR_OK;
        }        
        int32_t reset() {
            std::lock_guard lock(mutex_);
            finish_done_ = false;
            generation_.fetch_add(1); reset_pending_ = true; reset_position_ = next_sample_ = 0;
            input_.clear(); output_.clear(); queued_ = 0; pinned_ = false;
            finish_pending_ = closed_ = false; failed_ = false; ready_.notify_one();
            return PLUGIN_ASR_OK;
        }
    protected:
        double inference_ms = 0, queue_ms = 0;
        Clock::time_point batch_enqueued = Clock::now();
    private:
        struct Chunk { std::vector<float> pcm; Clock::time_point enqueued; };
        void erase_drafts_locked(uint64_t id, bool all = false) {
            auto begin = output_.begin();
            if (pinned_ && begin != output_.end()) ++begin;
            output_.erase(std::remove_if(begin, output_.end(), [&](const Event& e) {
                return e.kind == Event::partial && (all || e.id == id);
                }), output_.end());
        }
        bool enqueue_locked(Event event) {
            // Reserve one slot for an explicit terminal error. Preserve already
            // queued finals; never clear the queue to make overflow look successful.
            if (output_.size() >= 127) {
                failed_ = true; input_.clear(); queued_ = 0; finish_pending_ = false;
                Event error; error.kind = Event::error;
                error.text = "Subtitle consumer overflow; reset required";
                output_.push_back(std::move(error)); return false;
            }
            output_.push_back(std::move(event)); return true;
        }
        void publish(Event event) {
            std::lock_guard lock(mutex_);
            if (cancelled()) return;
            if (event.kind == Event::partial || event.kind == Event::final) {
                event.inference_ms = inference_ms; event.queue_ms = queue_ms;
                erase_drafts_locked(event.id);
            }
            enqueue_locked(std::move(event));
        }
        void fail(const char* message) {
            std::lock_guard lock(mutex_);
            if (cancelled()) return; // Includes stale exceptions after reset.
            Event event; event.kind = Event::error; event.text = message;
            enqueue_locked(std::move(event));
            failed_ = true; input_.clear(); queued_ = 0; finish_pending_ = false;
        }
        void run() {
            Engine engine(*this, {}, [this](Event e) { publish(std::move(e)); });
            Highpass highpass;
            for (;;) {
                std::deque<Chunk> batch;
                bool reset_now, finish_now;
                int64_t position;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [&] { return stopping_ || reset_pending_ || !input_.empty() || finish_pending_; });
                    if (stopping_) break;
                    reset_now = reset_pending_; reset_pending_ = false; position = reset_position_;
                    working_generation_ = generation_.load();
                    batch.swap(input_); queued_ = 0;
                    finish_now = finish_pending_; finish_pending_ = false;
                }
                try {
                    if (reset_now) { engine.reset(position); highpass.reset(); inference_ms = queue_ms = 0; }
                    std::vector<float> pcm;
                    for (auto& chunk : batch) pcm.insert(pcm.end(), chunk.pcm.begin(), chunk.pcm.end());
                    batch_enqueued = batch.empty() ? Clock::now() : batch.front().enqueued;
                    if (!pcm.empty()) {
                        highpass.apply(pcm, 80);
                        engine.consume(pcm.data(), pcm.size());
                    }
                    if (finish_now && !cancelled()) {
                        engine.finish();
                        Event event;
                        event.kind = Event::drained;
                        publish(std::move(event));

                        // 处理完毕，通知 finish 解除阻塞
                        {
                            std::lock_guard lock(mutex_);
                            finish_done_ = true;
                            drained_cv_.notify_all();
                        }
                    }
                }
                catch (const std::exception& e) { fail(e.what()); }
                catch (...) { fail("Unknown ASR worker failure"); }
            }
        }
        std::mutex mutex_;
        std::condition_variable ready_;
        std::deque<Chunk> input_;
        std::deque<Event> output_;
        std::thread worker_;
        std::atomic<uint64_t> generation_{ 1 };
        std::atomic<bool> stopping_{ false }, failed_{ false };
        uint64_t working_generation_ = 1; // Read/written only by worker (and warmup).
        size_t queued_ = 0;
        int64_t next_sample_ = 0, reset_position_ = 0;
        bool reset_pending_ = true, finish_pending_ = false, closed_ = false, pinned_ = false;

        std::condition_variable drained_cv_;
        bool finish_done_ = false;
    };
}
