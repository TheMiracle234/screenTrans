// Standalone host example: Windows + C++20 only, no plugin import library.
#pragma once
#include <chrono>
#include <cstdint>

namespace example {
    // Wall-clock deadlines for incremental PCM delivery. Kept separate so a host
    // stall can be simulated without relying on an actual OS scheduling pause.
    class ReplayClock {
    public:
        using Clock = std::chrono::steady_clock;
        explicit ReplayClock(Clock::time_point now) : next_(now) {}
        Clock::time_point after_push(uint32_t samples, Clock::time_point now) {
            const auto interval = std::chrono::microseconds(uint64_t(samples) * 1000000 / 16000);
            auto due = next_ + interval;
            // Correct ordinary timer jitter against the audio timeline. If a host
            // stall leaves less than half a period to wait, rebase instead of
            // catching up. Every push is followed by a meaningful wait.
            if (due - now < interval / 2) due = now + interval;
            next_ = due;
            return next_;
        }
    private:
        Clock::time_point next_;
    };
}

#define NOMINMAX
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib,"winmm.lib")
#include "plugin/asr.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct TimerResolution {
    bool active = timeBeginPeriod(1) == TIMERR_NOERROR;
    ~TimerResolution() { if (active) timeEndPeriod(1); }
};

struct Plugin {
    HMODULE dll = nullptr;
    plugin_asr_stream* stream = nullptr;
    decltype(&plugin_asr_open) open = nullptr;
    decltype(&plugin_asr_push) push = nullptr;
    decltype(&plugin_asr_poll) poll = nullptr;
    decltype(&plugin_asr_finish) finish = nullptr;
    decltype(&plugin_asr_reset) reset = nullptr;
    decltype(&plugin_asr_close) close = nullptr;
    Plugin() = default;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    ~Plugin() {
        if (stream) close(stream);
        if (dll) FreeLibrary(dll);
    }
    void load(const std::filesystem::path& file) {
        dll = LoadLibraryW(std::filesystem::absolute(file).c_str());
        if (!dll) throw std::runtime_error("LoadLibraryW failed: " + std::to_string(GetLastError()));
#define LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(dll,"plugin_asr_" #name)); \
    if (!name) throw std::runtime_error("Missing export: plugin_asr_" #name)
        LOAD(open); LOAD(push); LOAD(poll); LOAD(finish); LOAD(reset); LOAD(close);
#undef LOAD
    }
};
uint16_t u16(const unsigned char* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char* p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
std::vector<float> read_wav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char head[12];
    if (!file.read(reinterpret_cast<char*>(head), 12) || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4))
        throw std::runtime_error("Expected RIFF/WAVE file");
    const auto file_size = std::filesystem::file_size(path);
    const uint64_t riff_end = uint64_t(u32(head + 4)) + 8;
    if (riff_end > file_size || riff_end < 12) throw std::runtime_error("Truncated RIFF file");
    bool format = false, data = false;
    std::vector<float> pcm;
    while (uint64_t(file.tellg()) + 8 <= riff_end) {
        unsigned char chunk[8];
        if (!file.read(reinterpret_cast<char*>(chunk), 8)) throw std::runtime_error("Truncated WAV chunk");
        const auto bytes = u32(chunk + 4);
        const auto begin = uint64_t(file.tellg());
        if (begin + bytes + (bytes & 1) > riff_end) throw std::runtime_error("Invalid WAV chunk size");
        if (!std::memcmp(chunk, "fmt ", 4)) {
            unsigned char fmt[16];
            if (bytes < 16 || !file.read(reinterpret_cast<char*>(fmt), 16) || u16(fmt) != 1 ||
                u16(fmt + 2) != 1 || u32(fmt + 4) != 16000 || u32(fmt + 8) != 32000 || u16(fmt + 12) != 2 || u16(fmt + 14) != 16)
                throw std::runtime_error("WAV must be 16kHz mono PCM16 (no resampling in this example)");
            format = true;
        }
        else if (!std::memcmp(chunk, "data", 4)) {
            if (!format || data || bytes % 2) throw std::runtime_error("Invalid or repeated WAV data chunk");
            pcm.resize(bytes / 2);
            for (auto& sample : pcm) {
                unsigned char b[2];
                if (!file.read(reinterpret_cast<char*>(b), 2)) throw std::runtime_error("Truncated PCM");
                const int value = u16(b); sample = float(value >= 32768 ? value - 65536 : value) / 32768.f;
            }
            data = true;
        }
        file.seekg(static_cast<std::streamoff>(begin + bytes + (bytes & 1)));
    }
    if (!format || !data) throw std::runtime_error("Missing WAV format/data");
    return pcm;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 4 && argc != 5) { std::cerr << "Usage: asr_wav_example.exe <DLL> <model-directory> <16kHz-mono-PCM16.wav> [threads:0..64]\n"; return 2; }
        int threads = 0;
        if (argc == 5) {
            size_t used = 0; const std::wstring arg = argv[4]; threads = std::stoi(arg, &used);
            if (used != arg.size() || threads < 0 || threads>64) throw std::runtime_error("threads must be 0..64");
        }
        SetConsoleOutputCP(CP_UTF8);
        auto pcm = read_wav(argv[3]);
        Plugin api; api.load(argv[1]);
        // Pass the supplied spelling to exercise the DLL's own relative-path
        // handling. Relative paths are based on the process working directory.
        const auto directory = std::filesystem::path(argv[2]).u8string();
        char error[1024];
        if (api.open(reinterpret_cast<const char*>(directory.c_str()), threads, &api.stream, error, sizeof(error)) != PLUGIN_ASR_OK)
            throw std::runtime_error(error);
        // Start a fresh recording, also demonstrating reset without reloading.
        if (api.reset(api.stream) != PLUGIN_ASR_OK) throw std::runtime_error("reset failed");
        // A default ~15.6ms Windows timer quantum cannot represent 10ms input.
        // Restore the requested resolution on every normal/exception exit.
        TimerResolution timer_resolution;
        if (!timer_resolution.active) std::cerr << "Cannot request 1ms timer resolution; replay may run slower than real time.\n";
        std::atomic<int32_t> producer_status{ PLUGIN_ASR_OK };
        std::atomic<bool> producer_done{ false };
        double feed_seconds = 0;
        uint64_t pushed_samples = 0, overflow_pushes = 0;
        const auto dll_path = std::filesystem::absolute(argv[1]).u8string();
        std::cout << std::fixed << std::setprecision(3)
            << "dll=" << reinterpret_cast<const char*>(dll_path.c_str()) << '\n'
            << "audio_s=" << double(pcm.size()) / 16000 << " chunk_ms=10 threads=" << threads;
#ifdef NDEBUG
        std::cout << " example_build=Release\n";
#else
        std::cout << " example_build=Debug (use Release for real-time performance)\n";
#endif
        // imguiTest: use its existing giveFrames() producer instead of this WAV
        // loop. Push only each NEW batch, including captured silence, once.
        std::jthread feeder([&](std::stop_token stop) {
            const auto start = example::ReplayClock::Clock::now();
            example::ReplayClock pacing(start);
            for (size_t i = 0;i < pcm.size() && !stop.stop_requested();i += 160) {
                const auto count = static_cast<uint32_t>(std::min<size_t>(160, pcm.size() - i));
                const auto status = api.push(api.stream, pcm.data() + i, count);
                if (status != PLUGIN_ASR_OK && status != PLUGIN_ASR_OVERFLOW) {
                    producer_status = status; producer_done = true; return;
                }
                pushed_samples += count;
                if (status == PLUGIN_ASR_OVERFLOW) ++overflow_pushes;
                // Pace against the audio clock; a long stall rebases the clock
                // rather than causing immediate catch-up pushes.
                std::this_thread::sleep_until(pacing.after_push(count, example::ReplayClock::Clock::now()));
            }
            feed_seconds = std::chrono::duration<double>(example::ReplayClock::Clock::now() - start).count();
            if (!stop.stop_requested()) producer_status = api.finish(api.stream);
            producer_done = true;
            });
        std::map<uint64_t, std::pair<uint64_t, std::string>> drafts;
        std::vector<char> text(64);
        bool ended = false;
        uint64_t gap_events = 0, finals = 0;
        double max_inference_ms = 0, max_queue_ms = 0;
        auto last_gap = std::chrono::steady_clock::time_point::min();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(pcm.size() / 16) + std::chrono::seconds(120);
        // imguiTest: perform this polling/display work on its UI thread;
        // process available events each frame and keep drafts separate from finals.
        while (!ended) {
            if (producer_done && producer_status != PLUGIN_ASR_OK) throw std::runtime_error("audio producer failed");
            if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("drain timeout");
            plugin_asr_result r{}; r.struct_size = sizeof(r);
            const auto status = api.poll(api.stream, &r, text.data(), static_cast<uint32_t>(text.size()));
            if (status == PLUGIN_ASR_BUFFER_SMALL) { text.resize(r.text_bytes); continue; }
            if (status == PLUGIN_ASR_EMPTY) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
            if (status != PLUGIN_ASR_EVENT) throw std::runtime_error("poll failed");
            max_inference_ms = std::max(max_inference_ms, r.inference_ms);
            max_queue_ms = std::max(max_queue_ms, r.queue_ms);
            switch (r.type) {
            case PLUGIN_ASR_PARTIAL: {
                auto& draft = drafts[r.utterance_id];
                if (r.revision > draft.first) {
                    draft = { r.revision,text.data() };
                    std::cout << "draft[" << r.utterance_id << "] " << text.data() << '\n';
                }
                break;
            }
            case PLUGIN_ASR_FINAL:
                drafts.erase(r.utterance_id);
                if (*text.data()) { ++finals; std::cout << "final[" << r.utterance_id << "] " << text.data() << '\n'; }
                break; // An empty final clears the draft without appending text.
            case PLUGIN_ASR_GAP: {
                drafts.clear(); ++gap_events;
                const auto now = std::chrono::steady_clock::now();
                if (gap_events == 1 || now - last_gap >= std::chrono::seconds(2)) {
                    std::cerr << text.data() << " (total=" << gap_events << "; repeated warnings rate-limited)\n";
                    last_gap = now;
                }
                break;
            }
            case PLUGIN_ASR_ERROR: throw std::runtime_error(text.data()); // reset needed before reusing stream
            case PLUGIN_ASR_END: ended = true; break;
            }
        }
        feeder.join(); // All producer/poller calls end BEFORE close/FreeLibrary.
        std::cout << "summary: audio_s=" << double(pcm.size()) / 16000 << " feed_s=" << feed_seconds
            << " pushed_samples=" << pushed_samples << " overflow_pushes=" << overflow_pushes
            << " gaps=" << gap_events << " finals=" << finals
            << " max_inference_ms=" << max_inference_ms << " max_queue_ms=" << max_queue_ms << '\n';
        if (gap_events) std::cerr << "Audio was lost during overload. Check Release DLL, CPU load and thread count; END does not mean lossless recognition.\n";
        std::cout << "stream ended\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
