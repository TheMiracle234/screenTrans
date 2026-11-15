#pragma once
#include "whisper.h"
#include "utf8_text.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace asr {
    inline std::string path_utf8(const std::filesystem::path& path) {
        const auto text = path.u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }
#ifdef _WIN32
    inline const char model_module_anchor = 0;
    inline std::filesystem::path module_filename(bool plugin) {
        HMODULE module = nullptr;
        if (plugin && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&model_module_anchor), &module))
            throw std::runtime_error("Cannot locate ASR DLL module");
        std::vector<wchar_t> name(512);
        for (;;) {
            const auto n = GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
            if (!n) throw std::runtime_error("Cannot resolve ASR module filename");
            if (n < name.size()) return std::filesystem::path(std::wstring(name.data(), n));
            if (name.size() >= 32768) throw std::runtime_error("ASR module filename is too long");
            name.resize(std::min<size_t>(32768, name.size() * 2));
        }
    }
    inline std::filesystem::path module_directory(bool plugin) { return module_filename(plugin).parent_path(); }
#endif
    inline std::vector<std::filesystem::path> model_candidates(const std::filesystem::path& supplied,
        const std::filesystem::path& cwd, const std::filesystem::path& exe,
        const std::filesystem::path& dll) {
        std::vector<std::filesystem::path> paths;
        auto add = [&](const std::filesystem::path& path) {
            auto absolute = std::filesystem::absolute(path).lexically_normal();
            if (std::find(paths.begin(), paths.end(), absolute) == paths.end()) paths.push_back(std::move(absolute));
        };
        if (supplied.is_absolute()) add(supplied);
        else { add(cwd / supplied); add(exe / supplied); add(dll / "model"); add(dll); }
        return paths;
    }
    inline std::filesystem::path select_model_directory(const std::vector<std::filesystem::path>& paths) {
        std::ostringstream failures;
        for (const auto& path : paths) {
            std::vector<std::string> missing;
            for (const auto* filename : {"ggml-base.bin", "ggml-silero-v6.2.0.bin"}) {
                std::error_code ec;
                if (!std::filesystem::is_regular_file(path / filename, ec)) missing.emplace_back(filename);
            }
            if (missing.empty()) return path;
            failures << " [" << path_utf8(path) << ": missing ";
            for (size_t i = 0; i < missing.size(); ++i) { if (i) failures << ", "; failures << missing[i]; }
            failures << ']';
        }
        throw std::runtime_error("ASR model discovery failed; searched" + failures.str());
    }
    inline std::filesystem::path resolve_model_directory(const std::filesystem::path& supplied) {
        // Absolute input is authoritative, including when the named directory is missing.
        if (supplied.is_absolute()) return select_model_directory({ supplied.lexically_normal() });
#ifdef _WIN32
        return select_model_directory(model_candidates(supplied, std::filesystem::current_path(),
            module_directory(false), module_directory(true)));
#else
        return select_model_directory({ std::filesystem::absolute(supplied).lexically_normal() });
#endif
    }
    // std::ifstream(path) uses wide Windows paths. Whisper's narrow filename API
    // otherwise fails when the executable/model lives in a Chinese directory.
    class ModelFile {
    public:
        explicit ModelFile(const char* utf8)
            : file_(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8))), std::ios::binary) {
            loader.context = this;
            loader.read = [](void* self, void* out, size_t count) -> size_t {
                auto& file = static_cast<ModelFile*>(self)->file_;
                file.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
                return static_cast<size_t>(file.gcount());
                };
            loader.eof = [](void* self) { return static_cast<ModelFile*>(self)->file_.eof(); };
            loader.close = [](void*) {}; // RAII owns the file, including failure paths.
        }
        bool valid() const { return file_.is_open(); }
        whisper_model_loader loader{};
    private:
        std::ifstream file_;
    };
    inline whisper_context* load_model(const char* path, whisper_context_params params) {
        ModelFile file(path);
        // State is owned separately so cancelled inference can be discarded without
        // reloading model weights. Caller must create/free whisper_state explicitly.
        return file.valid() ? whisper_init_with_params_no_state(&file.loader, params) : nullptr;
    }
    inline whisper_vad_context* load_vad(const char* path, whisper_vad_context_params params) {
        ModelFile file(path);
        return file.valid() ? whisper_vad_init_with_params(&file.loader, params) : nullptr;
    }
}
