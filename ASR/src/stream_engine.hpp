#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace asr {
    constexpr int rate = 16000;
    constexpr int vad_frame = 512; // Silero's 32 ms window at 16 kHz.
    struct Settings {
        int first_ms = 512, step_ms = 400, silence_ms = 300;
        int pre_ms = 200, window_ms = 8000, min_ms = 96;
        bool hysteresis = true; float start = .65f, hold = .35f;
    };
    struct DecodeRequest {
        const std::vector<float>& pcm;
        const std::string& prompt;
        bool final;
        int64_t start, from, through; // absolute sample positions, [from, through).
        float voiced_ms = 0, vad_probability = 0, snr_db = 0;
    };
    struct Event {
        enum Kind { partial = 1, final = 2, gap = 3, error = 4, drained = 5 };
        int kind = partial;
        uint64_t id = 0, revision = 0;
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
        virtual bool cancelled() const = 0;
        virtual bool allow_partial() { return true; }
    };
    // All segmentation state belongs to the worker. Producers never mutate it.
    class Engine {
    public:
        Engine(Backend& backend, Settings settings, std::function<void(Event)> emit)
            : backend_(backend), settings_(settings), emit_(std::move(emit)) {
        }
        void reset(int64_t position = 0) {
            active_ = false;
            frame_.clear(); audio_.clear(); pre_.clear(); prompt_.clear();
            cursor_ = position; voiced_ = 0; revision_ = 0;
            votes_ = {}; vote_cursor_ = 0; observations_.clear(); noise_ = 1e-8;
            backend_.reset_vad();
        }
        void consume(const float* data, size_t count) {
            while (count && !backend_.cancelled()) {
                const size_t n = std::min(count, size_t(vad_frame) - frame_.size());
                frame_.insert(frame_.end(), data, data + n);
                data += n; count -= n;
                if (frame_.size() == vad_frame) { process_frame(); frame_.clear(); }
            }
            // Coalesce obsolete draft deadlines in a received batch.
            if (active_ && voiced_ >= settings_.first_ms * 16 && cursor_ >= next_draft_ && !backend_.cancelled() && backend_.allow_partial()) {
                recognize(false, cursor_);
                next_draft_ = cursor_ + settings_.step_ms * 16;
            }
        }
        void finish() {
            const int64_t actual_end = cursor_ + static_cast<int64_t>(frame_.size());
            if (!frame_.empty()) {
                const int valid = static_cast<int>(frame_.size());
                frame_.resize(vad_frame, 0.0f);
                process_frame(valid); frame_.clear();
            }
            cursor_ = actual_end;
            if (active_ && !backend_.cancelled()) end_utterance(cursor_);
        }
    private:
        void process_frame(int valid = vad_frame) {
            const float probability = settings_.hysteresis ? backend_.probability(frame_.data()) : (backend_.speech(frame_.data()) ? 1.f : 0.f);
            double power = 0; for (float f : frame_) power += double(f) * f; power /= vad_frame;
            votes_[vote_cursor_++ % 3] = probability >= settings_.start;
            const bool speech = settings_.hysteresis ? (active_ ? probability >= settings_.hold : std::count(votes_.begin(), votes_.end(), true) >= 2) : probability >= .5f;
            if (!active_ && probability < .1f) noise_ = .98 * noise_ + .02 * power;
            observations_.push_back({ cursor_,probability,power });
            while (!observations_.empty() && observations_.front().position < cursor_ - settings_.window_ms * 16) observations_.pop_front();
            if (!active_ && speech) {
                active_ = true; ++id_; revision_ = 0; voiced_ = settings_.hysteresis ? (std::count(votes_.begin(), votes_.end(), true) - 1) * vad_frame : 0;
                audio_.assign(pre_.begin(), pre_.end());
                start_ = cursor_ - static_cast<int64_t>(audio_.size()); from_ = start_;
                next_draft_ = cursor_ + settings_.first_ms * 16;
            }
            if (active_) {
                audio_.insert(audio_.end(), frame_.begin(), frame_.begin() + valid);
                if (speech) { last_voice_ = cursor_ + valid; voiced_ += valid; }
            }
            cursor_ += valid;
            pre_.insert(pre_.end(), frame_.begin(), frame_.begin() + valid);
            while (pre_.size() > static_cast<size_t>(settings_.pre_ms * 16)) pre_.pop_front();
            if (!active_ || backend_.cancelled()) return;
            if (cursor_ - last_voice_ >= settings_.silence_ms * 16) {
                end_utterance(cursor_);
            }
            else if (audio_.size() >= static_cast<size_t>(settings_.window_ms * 16)) {
                // Defer the last 320 ms; re-decode it with left context next time.
                const int64_t boundary = cursor_ - 320 * 16;
                recognize(true, boundary);
                const int64_t next_start = boundary - settings_.pre_ms * 16;
                audio_.erase(audio_.begin(), audio_.begin() + (next_start - start_));
                start_ = next_start; from_ = boundary; ++id_; revision_ = 0;
                next_draft_ = cursor_ + settings_.step_ms * 16;
            }
        }
        void end_utterance(int64_t end) {
            if (voiced_ >= settings_.min_ms * 16) recognize(true, end);
            else if (revision_) { Event e; e.kind = Event::final; e.id = id_; e.revision = ++revision_; e.start = from_; e.end = end; emit_(std::move(e)); }
            active_ = false; audio_.clear(); voiced_ = 0;
            // Preserve VAD state across pauses; reset only on a stream break.
        }
        void recognize(bool final, int64_t through) {
            float voice = 0, prob = 0; double power = 0;
            for (const auto& o : observations_) if (o.position >= from_ && o.position < through && o.probability >= settings_.hold) { voice += 32; prob += o.probability; power += o.power; }
            const float n = voice / 32;
            const auto text = backend_.decode({ audio_, prompt_, final, start_, from_, through,voice,n ? prob / n : 0,float(10 * std::log10((n ? power / n : 0) + 1e-12) - 10 * std::log10(noise_ + 1e-12)) });
            if (backend_.cancelled()) return;
            Event event;
            event.kind = final ? Event::final : Event::partial;
            event.id = id_; event.revision = ++revision_;
            event.start = from_; event.end = through; event.text = text;
            emit_(std::move(event)); // Empty final removes an earlier draft too.
            if (final && !text.empty()) {
                prompt_ += text;
                if (prompt_.size() > 512) {
                    size_t cut = prompt_.size() - 512;
                    while (cut < prompt_.size() && (static_cast<unsigned char>(prompt_[cut]) & 0xc0) == 0x80) ++cut;
                    prompt_.erase(0, cut);
                }
            }
        }
        Backend& backend_;
        Settings settings_;
        std::function<void(Event)> emit_;
        std::vector<float> frame_, audio_;
        std::deque<float> pre_;
        std::string prompt_;
        struct Observation { int64_t position; float probability; double power; };
        std::deque<Observation> observations_;
        std::array<bool, 3> votes_{}; size_t vote_cursor_ = 0; double noise_ = 1e-8;
        bool active_ = false;
        uint64_t id_ = 0, revision_ = 0;
        int64_t cursor_ = 0, start_ = 0, from_ = 0, last_voice_ = 0, voiced_ = 0, next_draft_ = 0;
    };
}

