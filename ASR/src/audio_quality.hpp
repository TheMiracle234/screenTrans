#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
namespace asr {
    class Highpass {
        float x_ = 0, y_ = 0;
    public:
        void reset() { x_ = y_ = 0; }
        void apply(std::vector<float>& pcm, float hz) {
            if (hz == 0) return;
            const float a = std::exp(-2 * 3.14159265358979323846f * hz / 16000);
            for (float& x : pcm) { float y = a * (y_ + x - x_); x_ = x; y_ = y; x = std::clamp(y, -1.f, 1.f); }
        }
    };
    inline std::vector<uint32_t> content(const std::string& text) {
        std::vector<uint32_t> out; uint32_t cp = 0; int remaining = 0;
        for (unsigned char c : text) {
            if (c < 128) cp = c;
            else if ((c & 0xe0) == 0xc0) { cp = c & 31; remaining = 1; continue; }
            else if ((c & 0xf0) == 0xe0) { cp = c & 15; remaining = 2; continue; }
            else if ((c & 0xf8) == 0xf0) { cp = c & 7; remaining = 3; continue; }
            else { cp = (cp << 6) | (c & 63); if (--remaining > 0) continue; }
            if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0x20000 && cp <= 0x3134f)) out.push_back(cp);
        }
        return out;
    }
    // Joint evidence, never a single-character blacklist. Real repetitions survive
    // whenever voice and token evidence support them.
    inline const char* reject(const std::string& text, float voiced_ms, float vad, float snr, float confidence, float no_speech) {
        const auto chars = content(text);
        if (chars.empty()) return "punctuation-or-empty";
        if (voiced_ms < 96) return "impulse-under-96ms";
        if (voiced_ms < 300 && (vad < .78f || confidence < .55f || no_speech>.45f || snr < 5)) return "weak-short-answer";
        if (no_speech > .8f && confidence < .5f) return "no-speech-low-confidence";
        if (confidence < .18f || (snr < 3 && vad < .8f && confidence < .6f)) return "weak-voice-and-tokens";
        bool repeated = chars.size() >= 3 && std::all_of(chars.begin(), chars.end(), [&](auto c) {return c == chars.front();});
        if (repeated && (confidence < .55f || (vad < .8f && snr < 6))) return "unsupported-repetition";
        return nullptr;
    }
}

