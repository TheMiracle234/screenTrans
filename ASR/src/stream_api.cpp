#ifndef NDEBUG
#   error plugin_ASR should be compiled in Release mode only
#endif
#define PLUGIN_ASR_EXPORTS
#include "plugin/asr.h"
#include "stream_worker.hpp"
#include "model_file.hpp"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
    thread_local const asr::Backend* decoding_backend = nullptr;
    void backend_log(ggml_log_level level, const char* message, void*) {
        if (level < GGML_LOG_LEVEL_WARN) return;
        // Upstream labels an intentionally aborted encoder as "failed to encode".
        // Only hide these known cancellation messages for the cancelled call;
        // genuine uncancelled failures still log and become an ERROR event.
        if (decoding_backend && decoding_backend->cancelled() &&
            (std::strstr(message, "failed to encode") || std::strstr(message, "failed to decode"))) return;
        std::fputs(message, stderr);
    }
    struct DecodeLogScope {
        const asr::Backend* previous = decoding_backend;
        explicit DecodeLogScope(const asr::Backend* backend) { decoding_backend = backend; }
        ~DecodeLogScope() { decoding_backend = previous; }
    };
    void copy_error(char* out, uint32_t cap, const char* message) {
        if (out && cap) {
            const auto n = std::min<size_t>(cap - 1, std::strlen(message));
            std::memcpy(out, message, n); out[n] = 0;
        }
    }
    bool complete_utf8(const std::string& s) {
        int remaining = 0;
        uint32_t cp = 0, minimum = 0;
        for (unsigned char c : s) {
            if (remaining) {
                if ((c & 0xc0) != 0x80) return false;
                cp = (cp << 6) | (c & 0x3f);
                if (--remaining == 0 && (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))) return false;
            }
            else if (c < 0x80) {}
            else if (c >= 0xc2 && c <= 0xdf) { remaining = 1; cp = c & 0x1f; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { remaining = 2; cp = c & 0x0f; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { remaining = 3; cp = c & 7; minimum = 0x10000; }
            else return false;
        }
        return remaining == 0;
    }
    std::string utf8(const std::filesystem::path& path) {
        const auto text = path.u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }
}
struct plugin_asr_stream final : asr::Worker {
    whisper_context* ctx = nullptr;
    whisper_state* state = nullptr;
    whisper_vad_context* vad = nullptr;
    bool state_dirty = false;
    int n_threads = 1;
    ~plugin_asr_stream() override {
        stop();
        if (vad) whisper_vad_free(vad);
        if (state) whisper_free_state(state);
        if (ctx) whisper_free(ctx);
    }
    static bool abort_compute(void* self) { return static_cast<plugin_asr_stream*>(self)->cancelled(); }
    void reset_vad() override {
        whisper_vad_reset_state(vad);
        // Called only by the worker, after the old whisper call has returned.
        // Drop its KV/scheduler buffers before processing the replacement PCM.
        if (state_dirty) {
            if (state) whisper_free_state(state);
            state = nullptr;
            state = whisper_init_state(ctx);
            if (!state) throw std::runtime_error("Cannot reinitialize cancelled Whisper state");
            state_dirty = false;
        }
    }
    bool speech(const float* pcm) override { return probability(pcm) >= .5f; }
    float probability(const float* pcm) override {
        if (!whisper_vad_detect_speech_no_reset(vad, pcm, asr::vad_frame))
            throw std::runtime_error("Silero inference failed");
        const auto* probs = whisper_vad_probs(vad);
        const int count = whisper_vad_n_probs(vad);
        if (count < 1 || !probs) throw std::runtime_error("Silero returned no probabilities");
        return probs[count - 1];
    }
    std::string decode(const asr::DecodeRequest& request) override {
        if (cancelled()) return {};
        if (!state || request.pcm.empty() || request.start < 0 || request.from < request.start ||
            request.through < request.from || request.through > request.start + static_cast<int64_t>(request.pcm.size()))
            throw std::runtime_error("Invalid ASR decode window boundary");
        auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
        p.language = "zh"; p.translate = false; p.detect_language = false;
        p.n_threads = n_threads;
        p.no_context = true; p.single_segment = true;
        p.temperature = 0; p.temperature_inc = 0; p.greedy.best_of = 1;
        p.max_tokens = request.final ? 0 : 96;
        p.suppress_blank = true; p.suppress_nst = true;
        // Only rollover windows need token timestamps; normal drafts use the fast path.
        const bool bounded = request.from > request.start || request.through < request.start + static_cast<int64_t>(request.pcm.size());
        p.token_timestamps = bounded; p.no_timestamps = !bounded;
        // Style hint is independent of the meeting/test vocabulary.
        const auto* style = reinterpret_cast<const char*>(u8"\u4ee5\u4e0b\u662f\u666e\u901a\u8bdd\u4f1a\u8bae\u8bb0\u5f55\uff0c\u4f7f\u7528\u7b80\u4f53\u4e2d\u6587\u3002");
        std::vector<whisper_token> prompt(64), history(256);
        const int prefix = whisper_tokenize(ctx, style, prompt.data(), 64);
        int nt = whisper_tokenize(ctx, request.prompt.c_str(), history.data(), static_cast<int>(history.size()));
        prompt.resize(prefix > 0 ? prefix : 0);
        if (nt > 0) {
            const int keep = std::min(nt, 64 - static_cast<int>(prompt.size()));
            prompt.insert(prompt.end(), history.begin() + nt - keep, history.begin() + nt);
        }
        p.prompt_tokens = prompt.data(); p.prompt_n_tokens = static_cast<int>(prompt.size());
        p.n_max_text_ctx = 64;
        p.abort_callback = abort_compute; p.abort_callback_user_data = this;
        // whisper.cpp short clips need padding, not altered duration/sample rate.
        std::vector<float> pcm(request.pcm);
        pcm.resize(std::max<size_t>(pcm.size(), asr::rate), 0.0f);
        const auto begin = asr::Clock::now();
        queue_ms = asr::milliseconds(begin - batch_enqueued);
        DecodeLogScope logging(this);
        const int status = whisper_full_with_state(ctx, state, p, pcm.data(), static_cast<int>(pcm.size()));
        inference_ms = asr::milliseconds(asr::Clock::now() - begin);
        if (cancelled()) { state_dirty = true; return {}; }
        if (status != 0) { state_dirty = true; throw std::runtime_error("Whisper inference failed"); }
        std::string text;
        double token_sum = 0; int token_count = 0; float no_speech = 0;
        for (int segment = 0; segment < whisper_full_n_segments_from_state(state); ++segment) {
            no_speech = std::max(no_speech, whisper_full_get_segment_no_speech_prob_from_state(state, segment));
            for (int token = 0;token < whisper_full_n_tokens_from_state(state, segment);++token) if (whisper_full_get_token_id_from_state(state, segment, token) < whisper_token_eot(ctx)) { token_sum += whisper_full_get_token_data_from_state(state, segment, token).p; ++token_count; }
            if (!bounded) { text += whisper_full_get_segment_text_from_state(state, segment); continue; }
            std::string bytes;
            int64_t t0 = -1, t1 = -1;
            for (int token = 0; token < whisper_full_n_tokens_from_state(state, segment); ++token) {
                if (whisper_full_get_token_id_from_state(state, segment, token) >= whisper_token_eot(ctx)) continue;
                const auto data = whisper_full_get_token_data_from_state(state, segment, token);
                bytes += whisper_full_get_token_text_from_state(ctx, state, segment, token);
                if (t0 < 0) t0 = data.t0;
                t1 = data.t1;
                if (!complete_utf8(bytes)) continue;
                if (t0 < 0 || t1 < 0) throw std::runtime_error("Missing rollover token timestamps");
                const int64_t midpoint = request.start + (t0 + t1) * 80;
                if (midpoint >= request.from && midpoint < request.through) text += bytes;
                bytes.clear(); t0 = t1 = -1;
            }
        }
        const float confidence = token_count ? float(token_sum / token_count) : 0;
        const char* reason = asr::reject(text, request.voiced_ms, request.vad_probability, request.snr_db, confidence, no_speech);
        if (reason) text.clear();
        return text;
    }

};
int32_t PLUGIN_ASR_CALL plugin_asr_open(const char* directory, int32_t threads,
    plugin_asr_stream** out, char* error, uint32_t cap) {
    if (out) *out = nullptr;
    copy_error(error, cap, "");
    if (!directory || !*directory || !out || threads < 0 || threads > 64 || (!error && cap)) {
        copy_error(error, cap, "Invalid ASR arguments"); return PLUGIN_ASR_INVALID;
    }
    try {
        if (!complete_utf8(directory)) {
            copy_error(error, cap, "Model directory must be UTF-8"); return PLUGIN_ASR_INVALID;
        }
        const auto supplied = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(directory)));
        std::error_code path_error;
        const auto root = std::filesystem::absolute(supplied, path_error).lexically_normal();
        if (path_error || !root.is_absolute()) {
            copy_error(error, cap, "Cannot resolve model directory relative to current working directory"); return PLUGIN_ASR_INVALID;
        }
        const auto model = utf8(root / "ggml-base.bin");
        const auto vad_model = utf8(root / "ggml-silero-v6.2.0.bin");
        auto stream = std::make_unique<plugin_asr_stream>();
        stream->n_threads = threads ? threads : static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        // Registry and logging are process-global. No plugin scanning or GPU.
        static std::mutex loading;
        std::lock_guard guard(loading);
        static std::once_flag setup;
        std::call_once(setup, [] {
            whisper_log_set(backend_log, nullptr);
            ggml_log_set(backend_log, nullptr);
            ggml_backend_register(ggml_backend_cpu_reg());
            });
        auto params = whisper_context_default_params();
        params.use_gpu = false; params.flash_attn = false;
        stream->ctx = asr::load_model(model.c_str(), params);
        if (!stream->ctx) throw std::runtime_error("Cannot load ggml-base.bin");
        stream->state = whisper_init_state(stream->ctx);
        if (!stream->state) throw std::runtime_error("Cannot initialize Whisper state");
        if (!whisper_is_multilingual(stream->ctx)) throw std::runtime_error("Multilingual base model required");
        auto vp = whisper_vad_default_context_params(); vp.use_gpu = false; vp.n_threads = 1;
        stream->vad = asr::load_vad(vad_model.c_str(), vp);
        if (!stream->vad) throw std::runtime_error("Cannot load ggml-silero-v6.2.0.bin");
        std::vector<float> silence(asr::rate, 0.f);
        stream->probability(silence.data());
        stream->reset_vad();
        stream->decode({ silence, {}, false, 0, 0, asr::rate });
        stream->start(); *out = stream.release(); return PLUGIN_ASR_OK;
    }
    catch (const std::exception& e) { copy_error(error, cap, e.what()); }
    catch (...) { copy_error(error, cap, "Unknown ASR initialization failure"); }
    return PLUGIN_ASR_FAILED;
}
int32_t PLUGIN_ASR_CALL plugin_asr_push(plugin_asr_stream* s, const float* pcm, uint32_t n) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->push(pcm, n); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_ASR_CALL plugin_asr_poll(plugin_asr_stream* s, plugin_asr_result* out, char* text, uint32_t cap) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->poll(out, text, cap); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_ASR_CALL plugin_asr_finish(plugin_asr_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->finish(); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_ASR_CALL plugin_asr_reset(plugin_asr_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->reset(); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
void PLUGIN_ASR_CALL plugin_asr_close(plugin_asr_stream* s) {
    try { delete s; }
    catch (...) {}
}
