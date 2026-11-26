#pragma once

#include <util/export.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 便捷启动：按来源字符串自动判别后端。
//   source 以 http:// 或 https:// 开头 → 远程后端，remoteModel 指定模型；
//   否则视为本地 GGUF 路径（UTF-8，支持中文/空格）。
// remoteModel 仅远程生效；留空使用默认 "qwen2.5:7b"。
PLUGIN_API int32_t PLUGIN_CALL plugin_summary_start(const char* src, const char* remoteModel);
// 是否已就绪可生成。
PLUGIN_API int32_t PLUGIN_CALL plugin_summary_ready();
// 请求打断当前正在进行的 generate：不释放模型，正在推理的线程在最近的
// 检查点立即返回空串，取消后仍可再次 generate。线程安全，可从任意线程调用。
PLUGIN_API void PLUGIN_CALL plugin_summary_cancel();
// nullptr if error
PLUGIN_API const char* PLUGIN_CALL plugin_summary_generate(const char* prompt, const char* content);
PLUGIN_API const char* PLUGIN_CALL plugin_summary_lasterror();
// 停止并释放已加载的模型/连接资源。可重复调用。
PLUGIN_API int32_t PLUGIN_CALL plugin_summary_stop();

#ifdef __cplusplus
}
#endif