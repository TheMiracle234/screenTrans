#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace captions {
    constexpr uint32_t protocol = 0x464d0002;
    constexpr size_t max_wire = 256 * 1024;
    enum Kind : uint8_t { partial = 1, final = 2, cancel = 3, unavailable = 4 };
    struct Event {
        uint8_t kind = cancel;
        uint64_t source = 0, epoch = 0, id = 0, revision = 0, commit = 0;
        std::string text;
    };
    inline bool utf8(const std::string& s) {
        uint32_t cp = 0, minimum = 0; int remaining = 0;
        for (unsigned char c : s) {
            if (remaining) { if ((c & 0xc0) != 0x80) return false; cp = (cp << 6) | (c & 63); if (--remaining == 0 && (cp < minimum || cp>0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))) return false; }
            else if (c < 128) { if (c == 0 || (c < 32 && c != '\n' && c != '\t')) return false; }
            else if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; remaining = 1; minimum = 128; }
            else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; remaining = 2; minimum = 2048; }
            else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; remaining = 3; minimum = 65536; }
            else return false;
        }
        return remaining == 0;
    }
    inline std::vector<uint8_t> encode(const std::vector<Event>& events) {
        std::vector<uint8_t> b;
        auto number = [&](uint64_t n) { for (int i = 7;i >= 0;--i) b.push_back(uint8_t(n >> (i * 8))); };
        number(events.size());
        for (const auto& e : events) { 
            number(e.kind); 
            number(e.source); 
            number(e.epoch); 
            number(e.id); 
            number(e.revision); 
            number(e.commit); 
            number(e.text.size()); 
            b.insert(b.end(), e.text.begin(), e.text.end()); 
        }
        return b;
    }
    inline bool decode(const std::vector<uint8_t>& b, std::vector<Event>& out) {
        out.clear(); size_t pos = 0;
        auto number = [&](uint64_t& n) { n = 0; if (b.size() - pos < 8) return false; for (int i = 0;i < 8;++i) n = (n << 8) | b[pos++]; return true; };
        uint64_t count; if (b.size() > max_wire || !number(count) || count > 256) return false;
        std::vector<Event> pending;
        for (uint64_t i = 0;i < count;++i) {
            Event e; uint64_t kind, len;
            if (!number(kind) || kind<partial || kind>unavailable || !number(e.source) || !number(e.epoch) || !e.epoch || !number(e.id) || !number(e.revision) || !number(e.commit) || !number(len) || len > 4096 || len > b.size() - pos) return false;
            e.kind = uint8_t(kind); e.text.assign(reinterpret_cast<const char*>(b.data() + pos), size_t(len)); pos += size_t(len);
            if (!utf8(e.text)) return false; pending.push_back(std::move(e));
        }
        if (pos != b.size()) return false; out.swap(pending); return true;
    }
    // The server and display use the same monotonic transition rules. Tombstones
    // survive eviction from the 32-line history, so late drafts cannot resurrect it.
    class Store {
        struct Source { 
            uint64_t epoch = 0, completed = 0; 
            Event live; 
        };
        std::unordered_map<uint64_t, Source> sources_;
        std::deque<Event> finals_;
        uint64_t sequence_ = 0;
    public:
        bool apply(Event e, bool authority = false) {
            if (!e.epoch || e.text.size() > 4096 || !utf8(e.text) || e.kind<partial || e.kind>unavailable) return false;
            if (!sources_.contains(e.source) && sources_.size() >= 128) return false;
            auto& s = sources_[e.source];
            // Historical finals from a snapshot may precede the current epoch.
            if (!authority && e.kind == final && e.commit) {
                if (std::any_of(finals_.begin(), finals_.end(), [&](auto& x) {return x.commit == e.commit;})) return false;
                if (finals_.size() == 32 && e.commit < finals_.front().commit) return false;
                finals_.push_back(e); std::sort(finals_.begin(), finals_.end(), [](auto& a, auto& b) {return a.commit < b.commit;});
                while (finals_.size() > 32) finals_.pop_front();
            }
            if (e.epoch < s.epoch) return false;
            if (e.epoch > s.epoch) { s = {}; s.epoch = e.epoch; }
            if (e.kind == cancel || e.kind == unavailable) { s.completed = (std::max)(s.completed, e.id); if (s.live.id <= e.id) s.live = e; return true; }
            if (e.id <= s.completed) return false;
            if (s.live.id > e.id || (s.live.id == e.id && s.live.revision >= e.revision)) return false;
            if (e.kind == partial) { s.live = std::move(e); return true; }
            s.completed = e.id; s.live = {};
            if (authority) { e.commit = ++sequence_; if (!e.text.empty()) finals_.push_back(std::move(e)); while (finals_.size() > 32) finals_.pop_front(); }
            return true;
        }
        void depart(uint64_t source) { auto it = sources_.find(source); if (it != sources_.end()) { ++it->second.epoch; it->second.completed = 0; it->second.live = {}; } }
        std::vector<Event> snapshot() const {
            std::vector<Event> v(finals_.begin(), finals_.end());
            for (auto& [id, s] : sources_) {
                if (s.live.epoch) v.push_back(s.live);
                else if (s.epoch) { Event e; e.source = id; e.epoch = s.epoch; e.kind = cancel; e.id = s.completed; v.push_back(e); }
            }
            return v;
        }
        std::string render() const {
            std::string text;
            for (auto& e : finals_) if (!e.text.empty()) text += "[" + std::to_string(e.source) + "] " + e.text + "\n";
            for (auto& [id, s] : sources_) if (!s.live.text.empty() && (s.live.kind == partial || s.live.kind == unavailable)) text += "[" + std::to_string(id) + "] " + s.live.text + "\n";
            return text;
        }
    };
}
