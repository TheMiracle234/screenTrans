#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace offline_translator {

    struct Result {
        bool ok = false;
        std::string text;
        std::string error;
    };

    // One model translates one registered language direction, e.g. en -> de.
    // All text is UTF-8. No networking or automatic language detection is used.
    class Translator {
    public:
        Translator();
        ~Translator();

        Translator(const Translator&) = delete;
        Translator& operator=(const Translator&) = delete;
        Translator(Translator&&) noexcept;
        Translator& operator=(Translator&&) noexcept;

        // modelDirectory must contain CT2 model.bin/config.json and the two
        // SentencePiece files named source.spm and target.spm by default.
        bool registerModel(
            std::string sourceLanguage,
            std::string targetLanguage,
            const std::filesystem::path& modelDirectory,
            std::string* error = nullptr
        );

        // Releases a loaded model but keeps its registration.
        // The next translation for this language pair loads the model again.
        // Returns false if this language pair is not registered.
        bool unloadModel(
            std::string_view sourceLanguage,
            std::string_view targetLanguage,
            std::string* error = nullptr
        );

        // Removes a language pair registration and releases its loaded model.
        // If this pair was the default, the default languages are cleared.
        // Returns false if this language pair is not registered.
        bool unregisterModel(
            std::string_view sourceLanguage,
            std::string_view targetLanguage,
            std::string* error = nullptr
        );

        bool setDefaultLanguages(
            std::string sourceLanguage,
            std::string targetLanguage,
            std::string* error = nullptr
        );

        Result translate(
            std::string_view text,
            std::string_view sourceLanguage,
            std::string_view targetLanguage
        );

        Result translate(
            const char* data,
            std::size_t byteCount,
            std::string_view sourceLanguage,
            std::string_view targetLanguage
        );

        Result translate(std::string_view text);
        Result translate(const char* data, std::size_t byteCount);

        // Returns one result per input, preserving order. Empty entries are valid.
        std::vector<Result> translateBatch(
            const std::vector<std::string>& texts,
            std::string_view sourceLanguage,
            std::string_view targetLanguage
        );

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace offline_translator
