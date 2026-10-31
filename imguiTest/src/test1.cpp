// SPDX-License-Identifier: GPL-3.0-or-later
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <gl/Window.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#   pragma comment(lib, "legacy_stdio_definitions")
#endif

#if defined(_WIN32)
#   define FONT_CJK_PATH "C:/Windows/Fonts/msyh.ttc"
#elif defined(__APPLE__)
#   define FONT_CJK_PATH "/System/Library/Fonts/PingFang.ttc"
#elif defined(__linux__)
#   define FONT_CJK_PATH "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
#else
static_assert(false, "unsupported platform font");
#endif

#ifdef _WIN32
#   include <Windows.h>
#   include <timeapi.h>
#   pragma comment(lib, "winmm.lib")
#endif

#include <audio/Capture.hpp>
#include <audio/Player.hpp>
#include <audio/User.hpp>

#include <plugin/Loader.hpp>
#include <plugin/Asr.hpp>

#include <boost/cstdfloat.hpp>
#include <boost/circular_buffer.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace audio {
    inline constexpr uint32_t sampleRate = 16'000;
    inline constexpr uint32_t channels = 1;
    inline constexpr uint32_t periodSizeInFrames = 160;
    static_assert(sampleRate% periodSizeInFrames == 0);
    inline constexpr boost::float64_t periodSec = static_cast<boost::float64_t>(periodSizeInFrames) / sampleRate;
    inline constexpr double    bufSec = 1.0;
}

namespace ImGui {
    static void DockingSpace() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags dock_flags =
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("MainDockSpace", nullptr, dock_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
}

static std::string env_or_default(const char* key, const std::string& fallback) {
    if (const char* v = std::getenv(key); v && *v) return v;
    return fallback;
}

int main() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    // ==================== 窗口 / OpenGL ====================
    gl::GlfwInitGuard glfwInitGuard;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto window = std::make_unique<gl::Window>(1200, 600, glm::ivec2{ 200, 100 }, "FlexMeet-local-asr");
    glfwMakeContextCurrent(window->m_get);
    GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // ==================== ImGui ====================
    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto* io = &ImGui::GetIO();
    io->ConfigFlags =
        ImGuiConfigFlags_ViewportsEnable |
        ImGuiConfigFlags_DockingEnable |
        ImGuiConfigFlags_NavEnableKeyboard |
        ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    auto* style = &ImGui::GetStyle();
    style->ScaleAllSizes(main_scale);
    style->FontScaleDpi = main_scale;

    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    io->Fonts->AddFontFromFileTTF(FONT_CJK_PATH, 15.0f, &fontConfig,
        io->Fonts->GetGlyphRangesChineseFull());

    ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    // ==================== 音频基础 ====================
    audio::User   audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
    audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames,
                               &audioUser, audio::User::callback };
    audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };

    audioPlayer.start();
    const bool captureStarted = ad_cpt.Start();

    std::atomic<bool>     play_audio{ false };   // 是否回放麦克风（监听）
    std::atomic<bool>     asr_enable{ true };    // 是否跑 ASR
    std::atomic<float>    capturePeak{ 0.0f };
    std::atomic<uint64_t> captureSamples{ 0 };

    // ==================== ASR 插件加载 ====================
    plugin::Loader            asrLoader;
    plugin::fn::AsrDefaults   asrDefaults{};
    plugin::fn::AsrCreate     asrCreate{};
    plugin::fn::AsrPush       asrPush{};
    plugin::fn::AsrPoll       asrPoll{};
    plugin::fn::AsrFlush      asrFlush{};
    plugin::fn::AsrReset      asrReset{};
    plugin::fn::AsrDestroy    asrDestroy{};
    plugin_asr_stream* asrStream = nullptr;
    std::string               asrStatus;

    {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto assets = std::filesystem::path(executable).parent_path() / "plugin";
        auto to_utf8 = [](const std::filesystem::path& p) {
            auto bytes = p.u8string();
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            };

        const auto dll = to_utf8(assets / "plugin_ASR.dll");
        bool loaded = asrLoader.load(dll.c_str());
        if (loaded) {
            loaded = asrLoader.get(asrDefaults, plugin::name::asrDefaults)
                && asrLoader.get(asrCreate, plugin::name::asrCreate)
                && asrLoader.get(asrPush, plugin::name::asrPush)
                && asrLoader.get(asrPoll, plugin::name::asrPoll)
                && asrLoader.get(asrFlush, plugin::name::asrFlush)
                && asrLoader.get(asrReset, plugin::name::asrReset)
                && asrLoader.get(asrDestroy, plugin::name::asrDestroy);
        }
        if (loaded) {
            plugin_asr_config config{}; asrDefaults(&config);
            const auto model = env_or_default("FLEXMEET_ASR_MODEL", to_utf8(assets / "ggml-base.bin"));
            const auto vad = env_or_default("FLEXMEET_ASR_VAD", to_utf8(assets / "ggml-silero-v6.2.0.bin"));
            config.model_path = model.c_str();
            config.vad_model_path = vad.c_str();
            config.n_threads = std::clamp(
                std::atoi(env_or_default("FLEXMEET_ASR_THREADS", std::to_string(config.n_threads)).c_str()),
                1, 64);
            config.use_gpu = env_or_default("FLEXMEET_ASR_GPU", "1") != "0";

            char error[1024]{};
            if (asrCreate(&config, &asrStream, error, sizeof(error)) != PLUGIN_ASR_OK) {
                asrStatus = std::string("ASR init failed: ") + error;
                std::cerr << asrStatus << "\nmodel: " << model << "\nVAD:   " << vad << '\n';
                asrStream = nullptr;
            }
        }
        else {
            asrStatus = "ASR plugin missing or incompatible";
            std::cerr << "ASR: cannot load exports from " << dll << '\n';
        }
    }

    // ==================== 环形缓冲 & 结果（照 CS 项目） ====================
    std::mutex                     mtx_asrLocal;
    boost::circular_buffer<float>  asrLocalAudio{ audio::sampleRate }; // 1 秒，满了丢最旧

    std::mutex  mtx_asrText;
    std::string asrPartial;   // 当前 partial
    std::string asrFinal;     // 累计 final
    std::vector<char> textBuf(4096); // 只在 asrThread 里用

    // ==================== 采集线程（只往环形缓冲里塞） ====================
    std::jthread audioThread([&]() {
        std::vector<float> audioFrames;
        auto last_time = std::chrono::steady_clock::now();

        while (!window->shouldClose()) {
            const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<boost::float64_t>(audio::periodSec));

            ad_cpt.giveFrames(audioFrames);
            captureSamples.fetch_add(audioFrames.size());

            float peak = 0.0f;
            for (float s : audioFrames) peak = std::max(peak, std::fabs(s));
            capturePeak.store(std::max(peak, capturePeak.load() * 0.90f));

            if (play_audio) {
                audioUser.pushFrames(audioFrames.data(), audioFrames.size());
            }

            if (asr_enable && !audioFrames.empty()) {
                std::lock_guard lk(mtx_asrLocal);
                asrLocalAudio.insert(asrLocalAudio.end(), audioFrames.begin(), audioFrames.end());
            }

            auto next_frame = last_time + target_interval;
            std::this_thread::sleep_until(next_frame);
            last_time = next_frame;
        }
        });

    // ==================== ASR 线程（取全部累积 → push → poll） ====================
    std::jthread asrThread([&]() {
        std::vector<float> pcm;
        while (!window->shouldClose()) {
            if (!asrStream || !asr_enable) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            pcm.clear();
            {
                std::lock_guard lk(mtx_asrLocal);
                pcm.assign(asrLocalAudio.begin(), asrLocalAudio.end());
                asrLocalAudio.clear();
            }

            // 没攒到有效音频就等下一轮（不在极端碎片上跑推理）
            if (pcm.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (asrPush(asrStream, pcm.data(), static_cast<uint32_t>(pcm.size())) < 0) {
                // 溢出/状态错乱：重置流并丢弃 partial
                asrReset(asrStream);
                std::lock_guard lk(mtx_asrText);
                asrPartial.clear();
            }

            for (;;) {
                plugin_asr_result r{}; r.struct_size = sizeof(r);
                int status = asrPoll(asrStream, &r, textBuf.data(),
                    static_cast<uint32_t>(textBuf.size()));
                if (status == PLUGIN_ASR_BUFFER_SMALL) {
                    if (r.text_bytes + 1 > textBuf.size())
                        textBuf.resize(r.text_bytes + 1);
                    continue;
                }
                if (status != PLUGIN_ASR_EVENT) break;

                const std::string txt(textBuf.data());
                std::lock_guard lk(mtx_asrText);
                if (r.type == PLUGIN_ASR_PARTIAL) {
                    asrPartial = txt;
                }
                else if (r.type == PLUGIN_ASR_FINAL) {
                    if (!asrFinal.empty()) asrFinal += ' ';
                    asrFinal += txt;
                    asrPartial.clear();
                    if (asrFinal.size() > 8192)
                        asrFinal.erase(0, asrFinal.size() - 4096);
                }
                else if (r.type == PLUGIN_ASR_GAP || r.type == PLUGIN_ASR_ERROR) {
                    asrPartial.clear();
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });

    // ==================== 主 UI 循环 ====================
    while (!window->shouldClose()) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui::DockingSpace();
        ImGui::Begin("asr_test");

        // ---- 控制区 ----
        if (ImGui::Button(play_audio ? "stop_play_audio" : "play_audio")) {
            play_audio = !play_audio;
        }
        ImGui::SameLine();
        {
            bool en = asr_enable.load();
            if (ImGui::Checkbox("ASR enabled", &en)) asr_enable = en;
        }
        ImGui::SameLine();
        if (ImGui::Button("clear transcript")) {
            std::lock_guard lk(mtx_asrText);
            asrFinal.clear();
            asrPartial.clear();
        }

        // ---- 状态 ----
        ImGui::Text("capture: %s | samples: %llu",
            captureStarted ? "running" : "NOT running",
            static_cast<unsigned long long>(captureSamples.load()));
        ImGui::ProgressBar(capturePeak.load(), ImVec2(-FLT_MIN, 0), "Microphone level");

        if (!asrStatus.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", asrStatus.c_str());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Transcript:");

        ImGui::BeginChild("##asrtext", ImVec2(0, 0), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushTextWrapPos(0.0f);
        {
            std::lock_guard lk(mtx_asrText);
            if (!asrFinal.empty())   ImGui::TextWrapped("%s", asrFinal.c_str());
            if (!asrPartial.empty()) ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                "%s", asrPartial.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();

        ImGui::End();
        ImGui::End(); // docking

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
        window->swapBuffers();
    }

    // ==================== 清理：先停线程，再 destroy 流 ====================
    audioThread.request_stop();
    asrThread.request_stop();
    audioThread.join();
    asrThread.join();
    if (asrStream) { asrDestroy(asrStream); asrStream = nullptr; }

    ImGui::DestroyPlatformWindows();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}