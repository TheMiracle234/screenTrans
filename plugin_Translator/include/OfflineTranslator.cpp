#include "OfflineTranslator.hpp"

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace offline_translator {
namespace {

using LanguagePair = std::pair<std::string, std::string>;

std::string normalizeLanguage(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 'a' - 'A')
                                      : static_cast<char>(c);
    });
    return result;
}

bool validLanguage(std::string_view value) {
    if (value.empty() || value.size() > 24) {
        return false;
    }
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

void assignError(std::string* output, std::string message) {
    if (output) {
        *output = std::move(message);
    }
}

struct Engine {
    sentencepiece::SentencePieceProcessor sourceTokenizer;
    sentencepiece::SentencePieceProcessor targetTokenizer;
    ctranslate2::Translator translator;

    explicit Engine(const std::filesystem::path& directory)
        : translator(directory.string(), ctranslate2::Device::CPU) {
        const auto sourcePath = (directory / "source.spm").string();
        const auto targetPath = (directory / "target.spm").string();

        const auto sourceStatus = sourceTokenizer.Load(sourcePath);
        if (!sourceStatus.ok()) {
            throw std::runtime_error(
                "Unable to load source.spm: " + sourceStatus.ToString()
            );
        }

        const auto targetStatus = targetTokenizer.Load(targetPath);
        if (!targetStatus.ok()) {
            throw std::runtime_error(
                "Unable to load target.spm: " + targetStatus.ToString()
            );
        }
    }

    std::vector<std::string> encode(const std::string& text) const {
        std::vector<std::string> tokens;
        const auto status = sourceTokenizer.Encode(text, &tokens);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece encode failed: " + status.ToString());
        }
        return tokens;
    }

    std::string decode(const std::vector<std::string>& tokens) const {
        std::string text;
        const auto status = targetTokenizer.Decode(tokens, &text);
        if (!status.ok()) {
            throw std::runtime_error("SentencePiece decode failed: " + status.ToString());
        }
        return text;
    }
};

// The OPUS-MT eng-zho model used by this SDK requires an explicit
// target-language token to select Mandarin Chinese in simplified script.
// Insert the token as a model token, NOT as text sent to SentencePiece.
std::vector<std::string> prepareSourceTokens(
    const Engine& engine,
    const LanguagePair& pair,
    const std::string& text
) {
    auto tokens = engine.encode(text);
    if (pair.first == "en" && pair.second == "zh") {
        tokens.insert(tokens.begin(), ">>cmn_Hans<<");
    }
    return tokens;
}

// Match the verified Python en->zh test: do not pass special EOS/PAD
// symbols into the SentencePiece decoder for this model.
std::string decodeTranslation(
    const Engine& engine,
    const LanguagePair& pair,
    const std::vector<std::string>& tokens
) {
    if (pair.first != "en" || pair.second != "zh") {
        return engine.decode(tokens);
    }

    std::vector<std::string> filtered;
    filtered.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (token != "</s>" && token != "<pad>") {
            filtered.push_back(token);
        }
    }
    return engine.decode(filtered);
}

// Match the Python en->zh verification (beam_size=4).  Keep the
// previously validated zh->en configuration unchanged.
ctranslate2::TranslationOptions translationOptions(const LanguagePair& pair) {
    ctranslate2::TranslationOptions options;
    if (pair.first == "en" && pair.second == "zh") {
        options.beam_size = 4;
    }
    return options;
}

Result failure(std::string description) {
    return Result{false, {}, std::move(description)};
}

}  // namespace

struct Translator::Impl {
    std::mutex mutex;
    std::map<LanguagePair, std::filesystem::path> paths;
    std::map<LanguagePair, std::unique_ptr<Engine>> engines;
    LanguagePair defaultLanguages;
    bool hasDefault = false;

    Engine& engineFor(const LanguagePair& pair) {
        auto current = engines.find(pair);
        if (current != engines.end()) {
            return *current->second;
        }
        const auto configured = paths.find(pair);
        if (configured == paths.end()) {
            throw std::runtime_error(
                "No local model registered for " + pair.first + " -> " + pair.second
            );
        }
        auto engine = std::make_unique<Engine>(configured->second);
        auto [it, inserted] = engines.emplace(pair, std::move(engine));
        (void)inserted;
        return *it->second;
    }
};

Translator::Translator() : impl_(std::make_unique<Impl>()) {}
Translator::~Translator() = default;
Translator::Translator(Translator&&) noexcept = default;
Translator& Translator::operator=(Translator&&) noexcept = default;

bool Translator::registerModel(
    std::string sourceLanguage,
    std::string targetLanguage,
    const std::filesystem::path& modelDirectory,
    std::string* error
) {
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        assignError(error, "Invalid language code. Use e.g. en, de, zh.");
        return false;
    }

    if (sourceLanguage == "auto" || targetLanguage == "auto") {
        assignError(error, "Automatic language detection is not supported.");
        return false;
    }

    for (const auto* name : {"config.json", "model.bin", "source.spm", "target.spm"}) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(modelDirectory / name, ec)) {
            assignError(error, "Missing model file: " + (modelDirectory / name).string());
            return false;
        }
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };
    std::lock_guard lock(impl_->mutex);
    impl_->paths[pair] = modelDirectory;
    impl_->engines.erase(pair);
    assignError(error, {});
    return true;
}

bool Translator::unloadModel(
    std::string_view sourceLanguage,
    std::string_view targetLanguage,
    std::string* error
) {
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        assignError(error, "Invalid language code.");
        return false;
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };
    std::lock_guard lock(impl_->mutex);
    if (!impl_->paths.contains(pair)) {
        assignError(error, "No local model registered for " + pair.first + " -> " + pair.second);
        return false;
    }

    // Destroy the Engine (model + both tokenizers), leaving its path registered.
    impl_->engines.erase(pair);
    assignError(error, {});
    return true;
}

bool Translator::unregisterModel(
    std::string_view sourceLanguage,
    std::string_view targetLanguage,
    std::string* error
) {
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        assignError(error, "Invalid language code.");
        return false;
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };
    std::lock_guard lock(impl_->mutex);
    if (!impl_->paths.contains(pair)) {
        assignError(error, "No local model registered for " + pair.first + " -> " + pair.second);
        return false;
    }

    // Erase engine before its registration to release loaded resources.
    impl_->engines.erase(pair);
    impl_->paths.erase(pair);

    if (impl_->hasDefault && impl_->defaultLanguages == pair) {
        impl_->defaultLanguages = {};
        impl_->hasDefault = false;
    }

    assignError(error, {});
    return true;
}

bool Translator::setDefaultLanguages(
    std::string sourceLanguage,
    std::string targetLanguage,
    std::string* error
) {
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        assignError(error, "Invalid source/target language code.");
        return false;
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };
    std::lock_guard lock(impl_->mutex);
    if (!impl_->paths.contains(pair)) {
        assignError(error, "Register this language pair before setting defaults.");
        return false;
    }
    impl_->defaultLanguages = pair;
    impl_->hasDefault = true;
    assignError(error, {});
    return true;
}

Result Translator::translate(
    std::string_view text,
    std::string_view sourceLanguage,
    std::string_view targetLanguage
) {
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        return failure("Invalid language code.");
    }
    if (text.empty()) {
        return Result{true, {}, {}};
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };
    std::lock_guard lock(impl_->mutex);
    try {
        Engine& engine = impl_->engineFor(pair);
        const auto tokens = prepareSourceTokens(engine, pair, std::string(text));
        const auto results = engine.translator.translate_batch({tokens}, translationOptions(pair));
        if (results.empty() || results.front().hypotheses.empty()) {
            return failure("Model returned no translation hypothesis.");
        }
        return Result{true, decodeTranslation(engine, pair, results.front().output()), {}};
    } catch (const std::exception& e) {
        return failure(e.what());
    }
}

Result Translator::translate(
    const char* data,
    std::size_t byteCount,
    std::string_view sourceLanguage,
    std::string_view targetLanguage
) {
    if (!data && byteCount != 0) {
        return failure("Nonzero byte count with null data pointer.");
    }
    return translate(
        data ? std::string_view(data, byteCount) : std::string_view{},
        sourceLanguage,
        targetLanguage
    );
}

Result Translator::translate(std::string_view text) {
    LanguagePair pair;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->hasDefault) {
            return failure("No default languages configured.");
        }
        pair = impl_->defaultLanguages;
    }
    return translate(text, pair.first, pair.second);
}

Result Translator::translate(const char* data, std::size_t byteCount) {
    if (!data && byteCount != 0) {
        return failure("Nonzero byte count with null data pointer.");
    }
    return translate(data ? std::string_view(data, byteCount) : std::string_view{});
}

std::vector<Result> Translator::translateBatch(
    const std::vector<std::string>& texts,
    std::string_view sourceLanguage,
    std::string_view targetLanguage
) {
    std::vector<Result> output;
    output.reserve(texts.size());
    if (texts.empty()) {
        return output;
    }
    if (!validLanguage(sourceLanguage) || !validLanguage(targetLanguage)) {
        output.assign(texts.size(), failure("Invalid language code."));
        return output;
    }

    const LanguagePair pair{
        normalizeLanguage(sourceLanguage), normalizeLanguage(targetLanguage)
    };

    std::lock_guard lock(impl_->mutex);
    try {
        Engine& engine = impl_->engineFor(pair);
        std::vector<std::vector<std::string>> tokens;
        std::vector<std::size_t> originalIndexes;
        output.resize(texts.size());

        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (texts[i].empty()) {
                output[i] = Result{true, {}, {}};
            } else {
                tokens.push_back(prepareSourceTokens(engine, pair, texts[i]));
                originalIndexes.push_back(i);
            }
        }
        if (!tokens.empty()) {
            const auto translated = engine.translator.translate_batch(tokens, translationOptions(pair));
            if (translated.size() != tokens.size()) {
                throw std::runtime_error("Model returned wrong batch size.");
            }
            for (std::size_t i = 0; i < translated.size(); ++i) {
                if (translated[i].hypotheses.empty()) {
                    output[originalIndexes[i]] = failure("No translation hypothesis.");
                } else {
                    output[originalIndexes[i]] = Result{
                        true,
                        decodeTranslation(engine, pair, translated[i].output()),
                        {}
                    };
                }
            }
        }
    } catch (const std::exception& e) {
        output.assign(texts.size(), failure(e.what()));
    }
    return output;
}

}  // namespace offline_translator
