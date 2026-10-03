#define PLUGIN_ASR_EXPORTS
#include "plugin/asr.h"

// whisper.cpp 的 C API 头文件
#include "whisper.h"

#include <string>
#include <vector>
#include <mutex>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <thread>

#include <iostream>
#ifndef NDEBUG
#	define print(x) std::cout<< x
#else
#	define print(x)
#endif

#define println(x) print(x << "\n")
#define PL println(__LINE__)

// ============================================================
// 内部状态
// ============================================================
static struct whisper_context* g_ctx = nullptr;
static std::string g_last_result;
static std::mutex g_mtx;  // 保护 g_ctx 和 g_last_result

// ============================================================
// 初始化
// ============================================================
int plugin_asr_init(const char* model_path) {
    std::lock_guard<std::mutex> lock(g_mtx);

    if (g_ctx) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }

    // 配置模型加载参数
    auto cparams = whisper_context_default_params();

    cparams.use_gpu = true;
    cparams.flash_attn = true;
    g_ctx = whisper_init_from_file_with_params(model_path, cparams);
    if (g_ctx) {
        printf("use: gpu flash_attn\n");
        return 0;
    }
    else {
        cparams.flash_attn = false;
        g_ctx = whisper_init_from_file_with_params(model_path, cparams);
    }

    if (g_ctx) {
        printf("use: gpu\n");
        return 0;
    }
    else {
        cparams.use_gpu = false;
        g_ctx = whisper_init_from_file_with_params(model_path, cparams);
    }

    if (g_ctx) {
        printf("use: cpu\n");
        return 0;
    }
    else {
        printf("use: error\n");
        return -1;
    }
}

const char* plugin_asr_transcribe(const float* pcm_data, int n_samples, int* out_bytes) {
    assert(out_bytes);
    std::lock_guard<std::mutex> lock(g_mtx);
    *out_bytes = 0;

    if (!g_ctx || !pcm_data || n_samples <= 0) {
        return nullptr;
    }

    constexpr int   kRate = 16000;   // 输入必须是 16kHz
    constexpr float kSilenceRms = 0.02f;  // 末尾静音 RMS 阈值
    constexpr float kVoicePeak = 0.01f;   // 前面判定语音的峰值阈值
    constexpr int   kTailMs = 150;     // 末尾静音检测窗口
    constexpr int   kMinSpeechMs = 200;     // 最短语音时长

    // ---- 1) 末尾 RMS 判断说话人是否已经停下 ----
    const int tail_n = std::min(n_samples, kRate * kTailMs / 1000);
    double sum_sq = 0.0;
    for (int i = n_samples - tail_n; i < n_samples; ++i) {
        sum_sq += static_cast<double>(pcm_data[i]) * pcm_data[i];
    }
    const float tail_rms = static_cast<float>(std::sqrt(sum_sq / tail_n));
    if (tail_rms > kSilenceRms) {
        // 末尾还有能量 → 句子没说完
        return nullptr;
    }

    // ---- 2) 前面是否真的有语音（过滤全静音输入） ----
    int voiced = 0;
    const int head_n = n_samples - tail_n;
    for (int i = 0; i < head_n; ++i) {
        if (std::fabs(pcm_data[i]) > kVoicePeak) ++voiced;
    }
    if (voiced < kRate * kMinSpeechMs / 1000) {
        // 语音太短，不算一句话
        return nullptr;
    }

    // ---- 3) whisper 识别 ----
    auto wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.translate = false;
    wparams.language = "zh";
    wparams.n_threads = std::min(16, (int)std::thread::hardware_concurrency());
    wparams.no_context = true;
    wparams.single_segment = true;

    if (whisper_full(g_ctx, wparams, pcm_data, n_samples) != 0) {
        return nullptr;
    }

    // ---- 4) 拼接所有片段 ----
    g_last_result.clear();
    const int n_seg = whisper_full_n_segments(g_ctx);
    for (int i = 0; i < n_seg; ++i) {
        const char* text = whisper_full_get_segment_text(g_ctx, i);
        if (text) g_last_result += text;
    }

    // ---- 5) 识别成功但结果为空：返回 ""，让调用方消费缓冲 ----
    if (g_last_result.empty()) {
        return "";   // 非 NULL + *out_bytes == 0
    }

    *out_bytes = static_cast<int>(g_last_result.size());
    return g_last_result.c_str();
}
// ============================================================
// 释放资源
// ============================================================
void plugin_asr_free() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_ctx) {
        whisper_free(g_ctx);
        g_ctx = nullptr;
    }
    g_last_result.clear();
}
