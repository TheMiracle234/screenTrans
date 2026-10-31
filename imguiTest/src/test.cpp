// Standalone host example: Windows + C++20 only, no plugin import library.
#define NOMINMAX
#include <Windows.h>
#include "plugin/asr.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
        if (argc != 4) { std::cerr << "Usage: asr_wav_example.exe <DLL> <model-directory> <16kHz-mono-PCM16.wav>\n"; return 2; }
        SetConsoleOutputCP(CP_UTF8);
        auto pcm = read_wav(argv[3]);
        Plugin api; api.load(argv[1]);
        const auto directory = std::filesystem::absolute(argv[2]).u8string();
        char error[1024];
        if (api.open(reinterpret_cast<const char*>(directory.c_str()), 0, &api.stream, error, sizeof(error)) != PLUGIN_ASR_OK)
            throw std::runtime_error(error);
        // Start a fresh recording, also demonstrating reset without reloading.
        if (api.reset(api.stream) != PLUGIN_ASR_OK) throw std::runtime_error("reset failed");
        std::atomic<int32_t> producer_status{ PLUGIN_ASR_OK };
        std::atomic<bool> producer_done{ false };
        // imguiTest: use its existing giveFrames() producer instead of this WAV
        // loop. Push only each NEW batch, including captured silence, once.
        std::jthread feeder([&](std::stop_token stop) {
            auto due = std::chrono::steady_clock::now();
            for (size_t i = 0;i < pcm.size() && !stop.stop_requested();i += 160) {
                const auto count = static_cast<uint32_t>(std::min<size_t>(160, pcm.size() - i));
                const auto status = api.push(api.stream, pcm.data() + i, count);
                if (status != PLUGIN_ASR_OK && status != PLUGIN_ASR_OVERFLOW) {
                    producer_status = status; producer_done = true; return;
                }
                due += std::chrono::microseconds(count * 1000000 / 16000);
                std::this_thread::sleep_until(due);
            }
            if (!stop.stop_requested()) producer_status = api.finish(api.stream);
            producer_done = true;
            });
        std::map<uint64_t, std::pair<uint64_t, std::string>> drafts;
        std::vector<char> text(64);
        bool ended = false;
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
                if (*text.data()) std::cout << "final[" << r.utterance_id << "] " << text.data() << '\n';
                break; // An empty final clears the draft without appending text.
            case PLUGIN_ASR_GAP: drafts.clear(); std::cerr << text.data() << '\n'; break;
            case PLUGIN_ASR_ERROR: throw std::runtime_error(text.data()); // reset needed before reusing stream
            case PLUGIN_ASR_END: ended = true; break;
            }
        }
        feeder.join(); // All producer/poller calls end BEFORE close/FreeLibrary.
        std::cout << "stream ended\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
