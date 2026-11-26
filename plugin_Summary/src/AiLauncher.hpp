// SPDX-License-Identifier: GPL-3.0-or-later
// AiLauncher：AI 启动器门面（统一入口）。
//
// 两种后端，同一套调用接口：
//   - 本地后端：进程内加载 GGUF 模型（llama.cpp，纯 CPU，离线，不联网）；
//   - 远程后端：连接已启动的 OpenAI 兼容服务（/v1/chat/completions，WinHTTP）。
//
// 对外只暴露 std::string / std::string_view / const char*。
// 路径按 UTF-8 处理，支持中文、空格等非 ASCII 路径（Windows 下内部转宽字符打开）。
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace minutes {

class LocalLlm;

// 启动来源配置。两种后端按需填写对应字段。
struct LlmLaunchConfig {
    // 本地后端：GGUF 模型文件路径（UTF-8）。远程后端留空。
    std::string modelPath;

    // 远程后端：OpenAI 兼容 base URL，如 http://localhost:11434/v1。本地留空。
    std::string baseUrl;

    // 远程后端：模型名（如 qwen2.5:7b）。本地后端忽略。
    std::string remoteModel;

    // 远程后端：可选 API Key；留空则不带 Authorization 头。
    std::string apiKey;

    // 本地后端：推理线程数，0 表示按硬件并发数自动选择。
    int nThreads = 0;

    // 本地后端：上下文长度（token 数）。
    int nCtx = 2048;

    // 远程后端：请求超时（毫秒）。
    int timeoutMs = 60000;
};

class AiLauncher {
public:
    enum class Backend {
        None,    // 未启动
        Local,   // 本地 GGUF 进程内推理
        Remote,  // 远程 OpenAI 兼容服务
    };

    AiLauncher();
    ~AiLauncher();

    AiLauncher(const AiLauncher&) = delete;
    AiLauncher& operator=(const AiLauncher&) = delete;

    // 便捷启动：按来源字符串自动判别后端。
    //   source 以 http:// 或 https:// 开头 → 远程后端，remoteModel 指定模型；
    //   否则视为本地 GGUF 路径（UTF-8，支持中文/空格）。
    // remoteModel 仅远程生效；留空使用默认 "qwen2.5:7b"。
    bool start(std::string_view source, std::string_view remoteModel = {});

    // 显式配置启动。
    bool start(const LlmLaunchConfig& cfg);

    // 停止并释放已加载的模型/连接资源。可重复调用。
    void stop();

    // 请求打断当前正在进行的 generate：不释放模型，正在推理的线程在最近的
    // 检查点立即返回空串，取消后仍可再次 generate。线程安全，可从任意线程调用。
    void cancel();

    // 最近一次 generate 是否被取消（供调用方区分正常结束 / 被强制截止）。
    bool cancelled() const;

    // 是否已就绪可生成。
    bool ready() const;

    // 当前后端类型（未启动为 None）。
    Backend backend() const { return backend_; }

    // 对话式生成一段文本。system 可传空串。
    //   maxTokens   : 最多生成多少个 token
    //   temperature : 采样温度，越低越稳定
    // 成功返回生成正文；失败返回空串，原因见 lastError()。
    std::string generate(std::string_view system,
                         std::string_view user,
                         int maxTokens = 256,
                         float temperature = 0.3f);

    // 最近一次失败原因描述。
    const std::string& lastError() const { return err_; }

    // 判断字符串是否为 http/https URL。
    static bool isRemoteUrl(std::string_view s);

    // 宽字符路径转 UTF-8：从 Win32 宽字符对话框 / API 取到的路径，
    // 先调用本函数转成 UTF-8，再传入 start()。
    static std::string wstringToUtf8(const std::wstring& w);

private:
    bool startLocal();
    bool startRemote();
    std::string remoteGenerate(std::string_view system,
                               std::string_view user,
                               int maxTokens,
                               float temperature);

    Backend backend_ = Backend::None;
    std::unique_ptr<LocalLlm> local_;
    LlmLaunchConfig cfg_;
    std::string err_;

    std::mutex mtx_;                          // 保护 local_/backend_/cfg_ 生命周期
    std::condition_variable cv_;               // 等待进行中的 generate 退出
    std::atomic<bool> cancel_{false};          // 门面取消标志
    std::atomic<int> active_{0};               // 正在执行 generate 的线程数
};

} // namespace minutes
