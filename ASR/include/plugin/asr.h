#pragma once

#ifdef _WIN32
    #ifdef PLUGIN_ASR_EXPORTS
        #define PLUGIN_ASR_API __declspec(dllexport)
    #else
        #define PLUGIN_ASR_API __declspec(dllimport)
    #endif
#endif

extern "C" {

// 初始化模型，加载 ggml 模型文件
// model_path: ggml-base.bin 等模型文件的路径
// 返回 0 成功，-1 失败
PLUGIN_ASR_API int plugin_asr_init(const char* model_path);

// 核心函数：输入 PCM float 数组（16kHz 单声道），返回识别文本（UTF-8）
// pcm_data: 32-bit float PCM 数组指针
// n_samples: 采样点数量
// 返回非空时表明已经是完整的一句话，否则不完整
// 返回的字符串由 DLL 内部管理，调用方不要 free，下次调用或释放时会被覆盖
PLUGIN_ASR_API const char* plugin_asr_transcribe(const float* pcm_data, int n_samples, int* out_bytes);

// 释放所有资源
PLUGIN_ASR_API void plugin_asr_free();

}

#undef PLUGIN_ASR_API