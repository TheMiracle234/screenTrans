#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace asr {
    constexpr int rate = 16000;
    constexpr int vad_frame = 512;
    struct Settings {
        int silence_ms = 500, pre_ms = 200, window_ms = 4000, min_ms = 96;
        bool hysteresis = true; float start = .65f, hold = .35f;
    };
    struct DecodeRequest {
        const std::vector<float>& pcm;
        const std::string& prompt;
        bool final;
        int64_t start, from, through;
        float voiced_ms = 0, vad_probability = 0, snr_db = 0;
    };
    struct DecodeJob {
        bool final = true;
        std::vector<float> pcm;
        uint64_t id = 0;
        int64_t start = 0, from = 0, through = 0;
        float voiced_ms = 0, vad_probability = 0, snr_db = 0;
    };
    struct Event {
        enum Kind { partial = 1, final = 2, gap = 3, error = 4, drained = 5 };
        int kind = final;
        uint64_t id = 0, revision = 0;
        uint64_t order = 0; // internal transport ordering, never part of the C ABI
        int64_t start = 0, end = 0;
        double inference_ms = 0, queue_ms = 0;
        std::string text;
    };
    struct Backend {
        virtual ~Backend() = default;
        virtual bool speech(const float* frame) = 0;
        virtual float probability(const float* frame) { return speech(frame) ? 1.f : 0.f; }
        virtual std::string decode(const DecodeRequest&) = 0;
        virtual void reset_vad() = 0;
        virtual void reset_decoder() {}
        virtual bool cancelled() const = 0;
    };
    // Audio-thread-only segmentation. Submission never runs Whisper. The decoder
    // owns inference state and each immutable job owns its PCM until inference ends.
    class Engine {
    public:
        Engine(Backend& backend, Settings settings, std::function<void(DecodeJob)> submit,
               std::function<bool()> cancelled, std::function<int(bool)> draft_admission = {})
            : backend_(backend), settings_(settings), submit_(std::move(submit)), cancelled_(std::move(cancelled)),
              draft_admission_(std::move(draft_admission)) {
            frame_.reserve(vad_frame); audio_.reserve(5 * rate);
        }
        void reset(int64_t position = 0) {
            active_ = false; frame_.clear(); audio_.clear(); pre_.clear();
            cursor_ = position; votes_ = {}; vote_cursor_ = 0;
            observations_.clear(); noise_ = 1e-8;
            backend_.reset_vad();
        }
        int64_t pending_from() const { return active_ ? from_ : cursor_; }
        void consume(const float* data, size_t count) {
            while (count && !cancelled_()) {
                const size_t n = std::min(count, size_t(vad_frame) - frame_.size());
                frame_.insert(frame_.end(), data, data + n);
                data += n; count -= n;
                if (frame_.size() == vad_frame) { process_frame(vad_frame); frame_.clear(); }
            }
        }
        void finish() {
            if (!frame_.empty() && !cancelled_()) {
                const int valid = static_cast<int>(frame_.size());
                frame_.resize(vad_frame, 0.f); process_frame(valid); frame_.clear();
            }
            if (active_ && !cancelled_()) { submit_final(cursor_); active_ = false; audio_.clear(); }
        }
    private:
        void process_frame(int valid) {
            const float probability = settings_.hysteresis ? backend_.probability(frame_.data()) : (backend_.speech(frame_.data()) ? 1.f : 0.f);
            double power = 0; for (float f : frame_) power += double(f) * f; power /= vad_frame;
            votes_[vote_cursor_++ % 3] = probability >= settings_.start;
            const bool speech = settings_.hysteresis ? (active_ ? probability >= settings_.hold : std::count(votes_.begin(), votes_.end(), true) >= 2) : probability >= .5f;
            if (!active_ && probability < .1f) noise_ = .98 * noise_ + .02 * power;
            observations_.push_back({cursor_, probability, power});
            while (!observations_.empty() && observations_.front().position < cursor_ - 5 * rate) observations_.pop_front();
            if (!active_ && speech) {
                active_ = true; audio_.assign(pre_.begin(), pre_.end());
                start_ = cursor_ - static_cast<int64_t>(audio_.size()); from_ = start_; ++id_;
                last_voice_ = cursor_;
                draft_origin_ = cursor_; last_draft_ = cursor_; first_draft_ = true;
            }
            if (active_) {
                audio_.insert(audio_.end(), frame_.begin(), frame_.begin() + valid);
                if (speech) last_voice_ = cursor_ + valid;
            }
            cursor_ += valid;
            pre_.insert(pre_.end(), frame_.begin(), frame_.begin() + valid);
            while (pre_.size() > static_cast<size_t>(settings_.pre_ms * 16)) pre_.pop_front();
            if (!active_ || cancelled_()) return;
            if (cursor_ - last_voice_ >= settings_.silence_ms * 16) {
                submit_final(cursor_); active_ = false; audio_.clear();
            }
            else if ((speech && cursor_ - from_ >= (settings_.window_ms + 512) * 16) || audio_.size() >= 5 * rate - vad_frame) {
                // Commit about 4 seconds with 512ms of real right lookahead,
                // rather than truncating the last phoneme at the decode boundary.
                // Right lookahead is committed by the following job, with 200ms
                // left context. Token midpoints partition the original timeline.
                const int64_t boundary = cursor_ - 512 * 16;
                submit_final(boundary);
                const int64_t next_start = std::max(start_, boundary - settings_.pre_ms * 16);
                audio_.erase(audio_.begin(), audio_.begin() + (next_start - start_));
                start_ = next_start; from_ = boundary; ++id_;
                draft_origin_ = from_; last_draft_ = from_; first_draft_ = true;
            }
            if (active_ && draft_admission_ && cursor_ - (first_draft_ ? draft_origin_ : last_draft_) >= 1024 * 16) {
                const int interval = draft_admission_(first_draft_);
                if (interval > 0 && cursor_ - (first_draft_ ? draft_origin_ : last_draft_) >= interval * 16) {
                    last_draft_ = cursor_; first_draft_ = false;
                    // Drafts contain only uncommitted audio; no DTW/overlap
                    // cropping is needed and no raw samples are committed.
                    auto job = make_job(cursor_); job.final = false;
                    const auto offset = size_t(from_ - start_);
                    job.pcm.erase(job.pcm.begin(), job.pcm.begin() + offset); job.start = from_;
                    submit_(std::move(job));
                }
            }
        }
        DecodeJob make_job(int64_t through) {
            float voice = 0, probability = 0; double power = 0;
            // A rollover tail can contain less than 96ms of NEW speech. Its
            // retained voiced left context is valid evidence too; rejecting it
            // as an isolated short answer silently loses the last character.
            for (const auto& o : observations_) if (o.position >= start_ && o.position < through && o.probability >= settings_.hold) {
                voice += 32; probability += o.probability; power += o.power;
            }
            const float n = std::max(1.f, voice / 32);
            DecodeJob job; job.pcm = audio_; job.id = id_; job.start = start_; job.from = from_; job.through = through;
            job.voiced_ms = voice; job.vad_probability = probability / n;
            job.snr_db = float(10 * std::log10(power / n + 1e-12) - 10 * std::log10(noise_ + 1e-12));
            return job;
        }
        void submit_final(int64_t through) {
            auto job = make_job(through);
            if (job.voiced_ms >= settings_.min_ms) submit_(std::move(job));
        }
        Backend& backend_;
        Settings settings_;
        std::function<void(DecodeJob)> submit_;
        std::function<bool()> cancelled_;
        std::function<int(bool)> draft_admission_;
        std::vector<float> frame_, audio_;
        std::deque<float> pre_;
        struct Observation { int64_t position; float probability; double power; };
        std::deque<Observation> observations_;
        std::array<bool, 3> votes_{}; size_t vote_cursor_ = 0;
        double noise_ = 1e-8; bool active_ = false;
        uint64_t id_ = 0;
        int64_t cursor_ = 0, start_ = 0, from_ = 0, last_voice_ = 0;
        int64_t draft_origin_ = 0, last_draft_ = 0;
        bool first_draft_ = true;
    };
}
