#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace asr {
    struct Utf8Scan { size_t complete_bytes = 0; bool invalid = false; };
    inline Utf8Scan scan_utf8(std::string_view text) {
        Utf8Scan result;
        int remaining = 0;
        uint32_t cp = 0, minimum = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (remaining) {
                if ((c & 0xc0) != 0x80) return { result.complete_bytes, true };
                cp = (cp << 6) | (c & 0x3f);
                if (--remaining == 0 && (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)))
                    return { result.complete_bytes, true };
            }
            else if (c < 0x80) {}
            else if (c >= 0xc2 && c <= 0xdf) { remaining = 1; cp = c & 0x1f; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { remaining = 2; cp = c & 0x0f; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { remaining = 3; cp = c & 7; minimum = 0x10000; }
            else return { result.complete_bytes, true };
            if (!remaining) result.complete_bytes = i + 1;
        }
        return result;
    }
    inline bool complete_utf8(std::string_view text) {
        const auto result = scan_utf8(text);
        return !result.invalid && result.complete_bytes == text.size();
    }
    inline void trim_incomplete_utf8_tail(std::string& text) {
        const auto result = scan_utf8(text);
        if (result.invalid) throw std::runtime_error("Whisper returned invalid UTF-8");
        // A decoder token limit may end between byte tokens of one character.
        // Preserve every complete character; never expose a half-character to poll.
        text.resize(result.complete_bytes);
    }
    inline void copy_error_utf8(char* out, uint32_t cap, const char* message) {
        if (!out || !cap) return;
        const auto n = scan_utf8(std::string_view(message, std::min<size_t>(cap - 1, std::strlen(message)))).complete_bytes;
        std::memcpy(out, message, n); out[n] = 0;
    }
}
