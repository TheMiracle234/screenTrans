#ifndef NDEBUG
#   error PLUGIN_ASR should be compiled in Release mode only
#endif
#define PLUGIN_EXPORTS
#include "plugin/asr.h"
#include "stream_worker.hpp"
#include "model_file.hpp"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#include <cstdio>
#include <memory>
#include <stdexcept>

#define NO_PROMPT 1

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
        asr::copy_error_utf8(out, cap, message);
    }
    using asr::complete_utf8;
    std::string utf8(const std::filesystem::path& path) {
        const auto text = path.u8string();
        return { reinterpret_cast<const char*>(text.data()), text.size() };
    }
}
struct PLUGIN_ASR_stream final : asr::Worker {
    struct Model {
        whisper_context* ctx = nullptr;
        whisper_state* state = nullptr;
        bool dirty = false;
        double load_ms = 0, state_ms = 0;
        ~Model() { if (state) whisper_free_state(state); if (ctx) whisper_free(ctx); }
        void load(const std::string& path) {
            auto p = whisper_context_default_params(); p.use_gpu = false; p.flash_attn = false;
            p.dtw_token_timestamps = true;
            p.dtw_aheads_preset = WHISPER_AHEADS_BASE;
            const auto begin = asr::Clock::now();
            ctx = asr::load_model(path.c_str(), p);
            load_ms = asr::milliseconds(asr::Clock::now() - begin);
            if (!ctx) throw std::runtime_error("Whisper model load failed: " + path + " (unreadable, incompatible or corrupt)");
            if (whisper_model_n_audio_layer(ctx) != 6 || whisper_model_n_text_layer(ctx) != 6 || !whisper_is_multilingual(ctx))
                throw std::runtime_error("Whisper model type mismatch: " + path);
            const auto state_begin = asr::Clock::now();
            state = whisper_init_state(ctx);
            state_ms = asr::milliseconds(asr::Clock::now() - state_begin);
            if (!state) throw std::runtime_error("Cannot initialize Whisper state: " + path);
        }
        void reset() {
            if (!dirty) return;
            if (state) whisper_free_state(state);
            state = nullptr; state = whisper_init_state(ctx);
            if (!state) throw std::runtime_error("Cannot reinitialize cancelled Whisper state");
            dirty = false;
        }
    } model;
    std::vector<whisper_token> prompt;
    whisper_vad_context* vad = nullptr;
    int n_threads = 1;
    ~PLUGIN_ASR_stream() override { stop(); if (vad) whisper_vad_free(vad); }
    static bool abort_compute(void* self) { return static_cast<PLUGIN_ASR_stream*>(self)->cancelled(); }
    void reset_vad() override { whisper_vad_reset_state(vad); }
    void reset_decoder() override { model.reset(); }
    void prepare_prompt() {
        //const auto* style = reinterpret_cast<const char*>(u8"软件工程课程会议。专业术语：插件化、私有化部署、语音识别、客户端、服务端。");
        const auto* style = reinterpret_cast<const char*>(u8"以下是一段多人视频会议的对话记录");
        prompt.resize(64);
        const int count = whisper_tokenize(model.ctx, style, prompt.data(), int(prompt.size()));
        if (count <= 0 || count > int(prompt.size())) throw std::runtime_error("ASR domain prompt exceeds token budget");
        prompt.resize(size_t(count));
#       if NO_PROMPT
            prompt.clear();
#       endif
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
        auto* ctx = model.ctx; auto* state = model.state;
        if (!state || request.pcm.empty() || request.start < 0 || request.from < request.start ||
            request.through < request.from || request.through > request.start + static_cast<int64_t>(request.pcm.size()))
            throw std::runtime_error("Invalid ASR decode window boundary");
        auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
        p.language = "zh"; p.translate = false; p.detect_language = false;
        p.n_threads = n_threads;
        p.audio_ctx = 0;
        p.no_context = true; p.single_segment = true;
        p.temperature = 0; p.temperature_inc = request.final ? .2f : 0.f; p.greedy.best_of = 1;
        p.max_tokens = request.final ? 0 : 48;
        p.suppress_blank = true; p.suppress_nst = true;
        // DTW alignment is independent of timestamp-token decoding. The older
        // heuristic assigns Chinese byte tokens to padded silence, losing words
        // when filtering short rollover windows.
        const bool bounded = request.final && (request.from > request.start || request.through < request.start + static_cast<int64_t>(request.pcm.size()));
        p.token_timestamps = false; p.no_timestamps = true;
        // Fixed domain vocabulary only: drafts and past recognition errors
        // cannot change the next segment's prompt or diverge across endpoints.
        p.prompt_tokens = prompt.data(); p.prompt_n_tokens = int(prompt.size());
        p.n_max_text_ctx = 64;
        p.abort_callback = abort_compute; p.abort_callback_user_data = this;
        // whisper.cpp short clips need padding, not altered duration/sample rate.
        std::vector<float> pcm(request.pcm);
        pcm.resize(std::max<size_t>(pcm.size() + asr::rate / 2, asr::rate), 0.0f);
        DecodeLogScope logging(this);
        const int status = whisper_full_with_state(ctx, state, p, pcm.data(), static_cast<int>(pcm.size()));
        if (cancelled()) { model.dirty = true; return {}; }
        if (status != 0) { model.dirty = true; throw std::runtime_error("Whisper inference failed"); }
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
                if (t0 < 0) t0 = data.t_dtw;
                t1 = data.t_dtw;
                if (!complete_utf8(bytes)) continue;
                if (t0 < 0 || t1 < 0) throw std::runtime_error("Missing rollover token timestamps");
                const int64_t midpoint = request.start + (t0 + t1) * 80;
                // At an utterance end, DTW can align the last character into
                // padding. Only a real right-lookahead boundary excludes tokens;
                // treating padded silence as a new window loses sentence endings.
                const bool left = request.from > request.start;
                const bool right = request.through < request.start + static_cast<int64_t>(request.pcm.size());
                if ((!left || midpoint >= request.from) && (!right || midpoint < request.through)) text += bytes;
                bytes.clear(); t0 = t1 = -1;
            }
        }
        asr::trim_incomplete_utf8_tail(text);
        const float confidence = token_count ? float(token_sum / token_count) : 0;
        const char* reason = asr::reject(text, request.voiced_ms, request.vad_probability, request.snr_db, confidence, no_speech);
        if (reason) text.clear();
        return text;
    }

};
int32_t PLUGIN_CALL plugin_asr_open(const char* directory, int32_t threads,
    PLUGIN_ASR_stream** out, char* error, uint32_t cap) {
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
        const auto root = asr::resolve_model_directory(supplied);
        const auto model = utf8(root / "ggml-base.bin");
        const auto vad_model = utf8(root / "ggml-silero-v6.2.0.bin");
        auto stream = std::make_unique<PLUGIN_ASR_stream>();
        stream->n_threads = threads ? threads : static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
#ifdef ASR_ENABLE_DIAGNOSTICS
        const auto dll = asr::path_utf8(asr::module_filename(true));
        const auto info = std::string(whisper_print_system_info()) + " model=base contexts=1 states=1 prompt=software-engineering-v1";
        stream->start_diagnostics(dll.c_str(), asr::path_utf8(root).c_str(), stream->n_threads, info.c_str());
#endif
        // Registry and logging are process-global. No plugin scanning or GPU.
        static std::mutex loading;
        std::lock_guard guard(loading);
        static std::once_flag setup;
        std::call_once(setup, [] {
            whisper_log_set(backend_log, nullptr);
            ggml_log_set(backend_log, nullptr);
            ggml_backend_register(ggml_backend_cpu_reg());
            });
        stream->model.load(model);
        stream->prepare_prompt();
        const auto vad_begin = asr::Clock::now();
        auto vp = whisper_vad_default_context_params(); vp.use_gpu = false; vp.n_threads = 1;
        stream->vad = asr::load_vad(vad_model.c_str(), vp);
        if (!stream->vad) throw std::runtime_error("Silero model load failed: " + vad_model + " (unreadable, incompatible or corrupt)");
        std::vector<float> silence(asr::rate, 0.f);
        stream->probability(silence.data());
        stream->reset_vad();
        stream->reset_decoder();
        const double vad_ms = asr::milliseconds(asr::Clock::now() - vad_begin);
        const auto warm_begin = asr::Clock::now();
        stream->decode({ silence, {}, false, 0, 0, asr::rate });
        const double warm_ms = asr::milliseconds(asr::Clock::now() - warm_begin);
        stream->set_draft_cost(warm_ms);
        stream->initialization_timings(stream->model.load_ms, stream->model.state_ms, vad_ms, warm_ms);
        stream->start(); *out = stream.release(); return PLUGIN_ASR_OK;
    }
    catch (const std::exception& e) { copy_error(error, cap, e.what()); }
    catch (...) { copy_error(error, cap, "Unknown ASR initialization failure"); }
    return PLUGIN_ASR_FAILED;
}
int32_t PLUGIN_CALL plugin_asr_push(PLUGIN_ASR_stream* s, const float* pcm, uint32_t n) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->push(pcm, n); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_CALL plugin_asr_poll(PLUGIN_ASR_stream* s, PLUGIN_ASR_result* out, char* text, uint32_t cap) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->poll(out, text, cap); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_CALL plugin_asr_finish(PLUGIN_ASR_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->finish(); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
int32_t PLUGIN_CALL plugin_asr_reset(PLUGIN_ASR_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try { return s->reset(); }
    catch (...) { return PLUGIN_ASR_FAILED; }
}
void PLUGIN_CALL plugin_asr_close(PLUGIN_ASR_stream* s) {
    try { delete s; }
    catch (...) {}
}
