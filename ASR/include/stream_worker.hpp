#pragma once
#include "plugin/asr.h"
#include "stream_engine.hpp"
#include "result_buffer.hpp"
#include "audio_quality.hpp"
#include "asr_diagnostics.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace asr {
    using Clock = std::chrono::steady_clock;
    inline double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
    struct WorkerLimits {
        int64_t samples = 8 * rate;
    };
    // One producer, one audio worker, one decoder, one poller. All indices and
    // queues use mutex_; neither VAD nor Whisper executes while it is held.
    class Worker : public Backend {
    public:
        explicit Worker(WorkerLimits limits = {}, bool drafts = true) : limits_(limits), drafts_enabled_(drafts), ring_(new float[size_t(limits.samples)]),
            times_(new Clock::time_point[size_t(limits.samples)]) {}
        ~Worker() override { stop(); }
        void start() {
            window_origin_ = Clock::now();
            audio_ = std::thread([this] { audio_loop(); });
            decoder_ = std::thread([this] { decode_loop(); });
        }
        void set_draft_cost(double ms) {
            std::lock_guard lock(mutex_); preview_ms_ = ms;
        }
        void initialization_timings(double load, double state, double vad, double warm) {
            diagnostics_.initialization(load, state, vad, warm);
        }
        void start_diagnostics(const char* dll, const char* models, int threads, const char* backend) {
            diagnostics_.start(dll, models, threads, backend);
        }
        void stop() {
            stopping_ = true; generation_.fetch_add(1); wake_.notify_all();
            if (audio_.joinable()) audio_.join();
            if (decoder_.joinable()) decoder_.join();
            diagnostics_.stop();
        }
        bool cancelled() const override {
            return stopping_ || failed_ || generation_.load() != decoding_generation_;
        }
        int32_t push(const float* pcm, uint32_t count) {
            if ((!pcm && count) || count > PLUGIN_ASR_MAX_SAMPLES) return PLUGIN_ASR_INVALID;
            for (uint32_t i = 0; i < count; ++i)
                if (!std::isfinite(pcm[i]) || std::abs(pcm[i]) > 1.f) return PLUGIN_ASR_INVALID;
            std::lock_guard lock(mutex_);
            if (closed_ || failed_ || stopping_) return PLUGIN_ASR_CLOSED;
            if (!count) return PLUGIN_ASR_OK;
            bool dropped = false;
            const int64_t excess = next_ - pending_ + protected_ + count - limits_.samples;
            if (excess > 0) { trim_locked(pending_ + excess); dropped = true; }
            // No allocations, backend work, or inference waits in the producer.
            const size_t at = size_t(next_ % limits_.samples);
            const size_t n = std::min<size_t>(count, size_t(limits_.samples) - at);
            const auto now = Clock::now();
            std::memcpy(ring_.get() + at, pcm, n * sizeof(float));
            std::memcpy(ring_.get(), pcm + n, (count - n) * sizeof(float));
            std::fill_n(times_.get() + at, n, now); std::fill_n(times_.get(), count - n, now);
            next_ += count;
            diagnostics_.input(count); measure_locked(); wake_.notify_all();
            return dropped ? PLUGIN_ASR_OVERFLOW : PLUGIN_ASR_OK;
        }
        int32_t poll(plugin_asr_result* out, char* text, uint32_t capacity) {
            if (!out || out->struct_size != sizeof(*out) || (!text && capacity)) return PLUGIN_ASR_INVALID;
            std::lock_guard lock(mutex_); diagnostics_.poll();
            *out = {}; out->struct_size = sizeof(*out);
            const auto* entry = output_.front();
            if (!entry) return PLUGIN_ASR_EMPTY;
            const auto& e = *entry;
            out->type = e.kind; out->utterance_id = e.id; out->revision = e.revision;
            out->start_sample = e.start; out->end_sample = e.end;
            out->inference_ms = e.inference_ms; out->queue_ms = e.queue_ms;
            out->text_bytes = static_cast<uint32_t>(e.text.size() + 1);
            if (!text || capacity < out->text_bytes) return PLUGIN_ASR_BUFFER_SMALL;
            std::memcpy(text, e.text.c_str(), out->text_bytes); output_.pop(); measure_locked();
            return PLUGIN_ASR_EVENT;
        }
        int32_t finish() {
            std::lock_guard lock(mutex_);
            if (failed_ || stopping_) return PLUGIN_ASR_CLOSED;
            closed_ = true; draft_job_.reset(); wake_.notify_all(); return PLUGIN_ASR_OK;
        }
        int32_t reset() {
            std::lock_guard lock(mutex_);
            generation_.fetch_add(1); ++audio_epoch_;
            next_ = pending_ = read_ = protected_ = 0;
            jobs_.clear(); draft_job_.reset(); output_.reset(); gap_begin_ = gap_end_ = 0;
            busy_.clear(); window_origin_ = Clock::now();
            closed_ = front_done_ = end_sent_ = false; failed_ = false;
            measure_locked(); wake_.notify_all(); return PLUGIN_ASR_OK;
        }
    protected:
        Diagnostics diagnostics_;
    private:
        struct Queued { DecodeJob job; Clock::time_point enqueued; uint64_t epoch = 0; };
        void measure_locked() {
            diagnostics_.queues(size_t(next_ - pending_ + protected_), output_.size());
            diagnostics_.stages(size_t(next_ - read_), jobs_.size(), size_t(protected_));
        }
        // The ring retains raw samples until commitment. Trimming only invalidates
        // unstarted jobs; the decoder's owned PCM and generation are untouched.
        void trim_locked(int64_t cut) {
            cut = std::min(next_, ((cut + vad_frame - 1) / vad_frame) * vad_frame);
            if (cut <= pending_) return;
            if (gap_begin_ == gap_end_) gap_begin_ = pending_;
            gap_end_ = cut;
            diagnostics_.dropped(uint64_t(cut - pending_));
            pending_ = read_ = cut; ++audio_epoch_; jobs_.clear(); draft_job_.reset(); front_done_ = false;
            measure_locked(); wake_.notify_all();
        }
        bool enqueue_locked(Event event) {
            if (failed_) return false;
            if (!output_.push(std::move(event))) {
                output_.clear_preview();
                Event error; error.kind = Event::error; error.text = "Subtitle consumer overflow; reset required";
                output_.force(std::move(error)); failed_ = true; jobs_.clear(); draft_job_.reset();
                measure_locked(); wake_.notify_all(); return false;
            }
            measure_locked(); return true;
        }
        bool draft_budget_locked(bool prospective) {
            const auto now = Clock::now(), cutoff = now - std::chrono::seconds(10);
            while (!busy_.empty() && busy_.front().second <= cutoff) busy_.pop_front();
            double ms = 0;
            for (const auto& span : busy_) ms += milliseconds(span.second - std::max(cutoff, span.first));
            if (active_decode_ && decoding_generation_ == generation_)
                ms += milliseconds(now - std::max({cutoff, active_begin_, window_origin_}));
            // Account for the next decode too: busy+p < .8*(elapsed+p).
            // Dispatch rechecks because a queued snapshot may have waited behind
            // a much slower call than the estimate available at submission.
            return ms + (prospective ? .2 * preview_ms_ : 0) <
                .8 * milliseconds(now - std::max(cutoff, window_origin_));
        }
        int draft_interval(bool first) {
            std::lock_guard lock(mutex_);
            if (!drafts_enabled_ || closed_ || failed_ || stopping_ || !jobs_.empty() || next_ - read_ > 6400 ||
                !draft_budget_locked(false)) return 0;
            // A busy decoder can retain one latest snapshot. Rejection does not
            // consume the segment's first opportunity or advance its cadence.
            return first ? 1024 : int(std::clamp(2 * preview_ms_, 1024., 2048.));
        }
        uint64_t next_revision_locked(uint64_t id) {
            if (revision_id_ != id) { revision_id_ = id; revision_ = 0; }
            return ++revision_;
        }
        void fail(const char* message, uint64_t generation, uint64_t epoch = 0) {
            std::lock_guard lock(mutex_);
            if (generation != generation_ || (epoch && epoch != audio_epoch_) || failed_ || stopping_) return;
            Event e; e.kind = Event::error; e.text = message; enqueue_locked(std::move(e));
            failed_ = true; jobs_.clear(); draft_job_.reset(); wake_.notify_all();
        }
        void audio_loop() {
            uint64_t local_generation = 0, local_epoch = 0;
            Engine engine(*this, {}, [&](DecodeJob job) {
                std::lock_guard lock(mutex_);
                if (generation_ != local_generation || audio_epoch_ != local_epoch || failed_ || stopping_) return;
                if (job.pcm.size() > 5 * rate) throw std::runtime_error("ASR job exceeds five-second bound");
                const auto sample = std::min(std::max(job.from, pending_), next_ - 1);
                Queued queued{std::move(job), times_[size_t(sample % limits_.samples)], local_epoch};
                if (queued.job.final) {
                    sealed_id_ = std::max(sealed_id_, queued.job.id);
                    if (draft_job_ && draft_job_->job.id <= sealed_id_) draft_job_.reset();
                    jobs_.push_back(std::move(queued));
                } else if (!closed_ && jobs_.empty() && queued.job.id > sealed_id_) draft_job_ = std::move(queued);
                wake_.notify_all();
            }, [&] { return stopping_ || failed_ || generation_ != local_generation || audio_epoch_ != local_epoch; }, [this](bool first) { return draft_interval(first); });
            Highpass highpass;
            std::vector<float> frame(vad_frame);
            for (;;) {
                try {
                    size_t n = 0; bool reset_now = false, finish_now = false; int64_t position = 0;
                    {
                        std::unique_lock lock(mutex_);
                        wake_.wait_for(lock, std::chrono::milliseconds(25), [&] {
                            return stopping_ || (!failed_ && (generation_ != local_generation || audio_epoch_ != local_epoch ||
                                read_ < next_ || (closed_ && !front_done_)));
                        });
                        if (stopping_) break;
                        if (failed_) continue;
                        reset_now = local_generation != generation_ || local_epoch != audio_epoch_;
                        local_generation = generation_; local_epoch = audio_epoch_; position = read_;
                        n = size_t(std::min<int64_t>(vad_frame, next_ - read_));
                        for (size_t i = 0; i < n; ++i) frame[i] = ring_[size_t((read_ + int64_t(i)) % limits_.samples)];
                        read_ += int64_t(n); finish_now = closed_ && read_ == next_ && !front_done_;
                    }
                    if (reset_now) { engine.reset(position); highpass.reset(); }
                    if (n) {
                        frame.resize(n); highpass.apply(frame, 80); engine.consume(frame.data(), n); frame.resize(vad_frame);
                        diagnostics_.audio(n);
                    }
                    if (finish_now) engine.finish();
                    {
                        std::lock_guard lock(mutex_);
                        if (generation_ != local_generation || audio_epoch_ != local_epoch || failed_) continue;
                        int64_t release = engine.pending_from();
                        if (!jobs_.empty()) release = std::min(release, jobs_.front().job.from);
                        pending_ = std::max(pending_, release);
                        if (finish_now) front_done_ = true;
                        measure_locked(); wake_.notify_all();
                    }
                }
                catch (const std::exception& e) { fail(e.what(), local_generation, local_epoch); }
                catch (...) { fail("Unknown ASR audio failure", local_generation, local_epoch); }
            }
        }
        void decode_loop() {
            for (;;) {
                Queued task; bool have_task = false, reset_now = false;
                uint64_t generation;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait_for(lock, std::chrono::milliseconds(25), [&] {
                        return stopping_ || (!failed_ && (generation_ != decoding_generation_ ||
                            !jobs_.empty() || (draft_job_ && next_ - read_ <= 6400 && draft_budget_locked(true)) ||
                            gap_begin_ != gap_end_ || (front_done_ && !end_sent_)));
                    });
                    if (stopping_) break;
                    if (failed_) continue;
                    generation = generation_;
                    reset_now = generation != decoding_generation_; decoding_generation_ = generation;
                    if (gap_begin_ != gap_end_) {
                        Event e; e.kind = Event::gap; e.start = gap_begin_; e.end = gap_end_;
                        e.text = "ASR audio budget exceeded; oldest unstarted audio dropped";
                        enqueue_locked(std::move(e)); diagnostics_.gap(); gap_begin_ = gap_end_ = 0;
                    }
                    if (failed_) continue;
                    if (!jobs_.empty()) {
                        task = std::move(jobs_.front()); jobs_.pop_front(); have_task = true;
                        protected_ = std::max<int64_t>(0, task.job.through - std::max(pending_, task.job.from));
                        pending_ = std::max(pending_, task.job.through);
                    } else if (draft_job_) {
                        if (next_ - read_ <= 6400 && draft_budget_locked(true)) {
                            task = std::move(*draft_job_); draft_job_.reset(); have_task = true;
                        }
                        // Keep the latest snapshot while temporarily over budget;
                        // the 25ms wait above also wakes without further pushes.
                    } else if (front_done_ && !end_sent_) {
                        Event e; e.kind = Event::drained; enqueue_locked(std::move(e)); end_sent_ = true;
                    }
                    if (have_task) { active_decode_ = true; active_begin_ = Clock::now(); }
                    measure_locked();
                }
                auto begin = Clock::now(); bool decode_started = false, published = false;
                const char* outcome = "complete";
                try {
                    if (reset_now) reset_decoder();
                    if (!have_task) continue;
                    begin = Clock::now(); decode_started = true;
                    diagnostics_.decode_begin();
                    const auto& j = task.job;
                    auto text = decode({j.pcm, {}, j.final, j.start, j.from, j.through, j.voiced_ms, j.vad_probability, j.snr_db});
                    const auto elapsed = milliseconds(Clock::now() - begin);
                    const bool stale = generation != generation_ || stopping_ || failed_;
                    outcome = stopping_ ? "close" : generation != generation_ ? "reset" : failed_ ? "failure" : "complete";
                    if (!stale) {
                        std::lock_guard lock(mutex_);
                        if (generation == generation_ && !stopping_ && !failed_ &&
                            (j.final || (task.epoch == audio_epoch_ && j.id > sealed_id_ && !closed_))) {
                            Event e; e.kind = j.final ? Event::final : Event::partial; e.id = j.id;
                            e.revision = next_revision_locked(j.id); e.start = j.from; e.end = j.through;
                            e.text = std::move(text); e.inference_ms = elapsed; e.queue_ms = milliseconds(begin - task.enqueued);
                            if (j.final) published = enqueue_locked(std::move(e));
                            else if (!e.text.empty()) { output_.preview(std::move(e)); published = true; }
                        }
                    }
                }
                catch (const std::exception& e) { outcome = "exception"; fail(e.what(), generation); }
                catch (...) { outcome = "exception"; fail("Unknown ASR decoder failure", generation); }
                const auto end = Clock::now();
                if (decode_started) diagnostics_.decode_end(milliseconds(end - begin), outcome,
                    task.job.from, task.job.through, milliseconds(begin - task.enqueued), task.job.final, published);
                {
                    std::lock_guard lock(mutex_);
                    if (have_task) {
                        active_decode_ = false;
                        if (generation == generation_) {
                            busy_.push_back({begin, end}); if (busy_.size() > 512) busy_.pop_front();
                            if (decode_started && !task.job.final && std::strcmp(outcome, "complete") == 0)
                                preview_ms_ = .8 * preview_ms_ + .2 * milliseconds(end - begin);
                        }
                    }
                    if (generation == generation_) protected_ = 0;
                    measure_locked(); wake_.notify_all();
                }
            }
        }
        WorkerLimits limits_;
        const bool drafts_enabled_;
        std::unique_ptr<float[]> ring_;
        std::unique_ptr<Clock::time_point[]> times_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<Queued> jobs_;
        ResultBuffer output_;
        std::optional<Queued> draft_job_;
        std::deque<std::pair<Clock::time_point, Clock::time_point>> busy_;
        Clock::time_point active_begin_, window_origin_ = Clock::now();
        bool active_decode_ = false;
        double preview_ms_ = 0;
        uint64_t sealed_id_ = 0, revision_id_ = 0, revision_ = 0;
        std::thread audio_, decoder_;
        std::atomic<uint64_t> generation_{1}, audio_epoch_{1};
        std::atomic<bool> stopping_{false}, failed_{false};
        uint64_t decoding_generation_ = 1; // decoder only; warmup happens before start
        int64_t next_ = 0, pending_ = 0, read_ = 0, protected_ = 0;
        int64_t gap_begin_ = 0, gap_end_ = 0;
        bool closed_ = false, front_done_ = false, end_sent_ = false;
    };
}
