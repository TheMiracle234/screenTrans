#pragma once
#include "stream_engine.hpp"
#include <optional>

namespace asr {
    // Protected by Worker's mutex. A metadata query pins a complete immutable
    // event, even when newer previews/finals arrive before the retry.
    class ResultBuffer {
    public:
        size_t size() const { return durable_.size() + size_t(bool(preview_)) + size_t(bool(retained_)); }
        bool push(Event e) {
            if (durable_count_ >= 127) return false;
            if (e.kind == Event::final) {
                if (preview_ && preview_->id == e.id) preview_.reset();
            } else if (e.kind == Event::gap || e.kind == Event::error || e.kind == Event::drained) clear_preview();
            force(std::move(e)); return true;
        }
        void force(Event e) { e.order = ++order_; if (e.kind != Event::partial) ++durable_count_; durable_.push_back(std::move(e)); }
        void preview(Event e) { e.order = ++order_; preview_ = std::move(e); }
        void clear_preview() {
            preview_.reset();
            uint64_t id = shown_id_, revision = shown_revision_;
            if (retained_ && retained_->kind == Event::partial && !retained_->text.empty()) {
                id = retained_->id; revision = retained_->revision;
            }
            if (!id || clearing_id_ == id) return;
            if (retained_ && retained_->kind == Event::final && retained_->id == id) return;
            if (std::any_of(durable_.begin(), durable_.end(), [id](const Event& e) { return e.kind == Event::final && e.id == id; })) return;
            Event clear; clear.kind = Event::partial; clear.id = id; clear.revision = revision + 1;
            force(std::move(clear)); clearing_id_ = id;
        }
        void reset() {
            durable_.clear(); preview_.reset(); retained_.reset(); durable_count_ = 0; clearing_id_ = 0;
            clear_preview(); // only a preview actually delivered to the host needs clearing
        }
        const Event* front() {
            if (!retained_) {
                if (preview_ && (durable_.empty() || preview_->order < durable_.front().order)) {
                    retained_ = std::move(preview_); preview_.reset();
                } else if (!durable_.empty()) { retained_ = std::move(durable_.front()); durable_.pop_front(); }
            }
            return retained_ ? &*retained_ : nullptr;
        }
        void pop() {
            const auto& e = *retained_;
            if (e.kind == Event::partial) {
                if (e.text.empty()) { if (shown_id_ == e.id) shown_id_ = 0; if (clearing_id_ == e.id) clearing_id_ = 0; }
                else { shown_id_ = e.id; shown_revision_ = e.revision; }
            } else {
                --durable_count_;
                if (e.kind == Event::final && shown_id_ == e.id) shown_id_ = 0;
            }
            retained_.reset();
        }
    private:
        std::deque<Event> durable_;
        std::optional<Event> preview_, retained_;
        size_t durable_count_ = 0;
        uint64_t order_ = 0, shown_id_ = 0, shown_revision_ = 0, clearing_id_ = 0;
    };
}
