#pragma once
#include "whisper.h"
#include <filesystem>
#include <fstream>

namespace asr {
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
