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

#ifdef _WIN32
#   include <Windows.h>
#   include <timeapi.h>
#   pragma comment(lib, "winmm.lib")
#endif

#include <audio/Capture.hpp>
#include <audio/Player.hpp>
#include <audio/User.hpp>

#include <plugin/asr.h>
#include <plugin/Loader.hpp>
#include <plugin/Asr.hpp>

#include <boost/cstdfloat.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace audio {
    inline constexpr uint32_t sampleRate = 16'000;
    inline constexpr uint32_t channels = 1;
    inline constexpr uint32_t periodSizeInFrames = 160;
    static_assert(sampleRate% periodSizeInFrames == 0);
    inline constexpr boost::float64_t periodSec =
        static_cast<boost::float64_t>(periodSizeInFrames) / sampleRate;
    inline constexpr double bufSec = 1.0;
}

namespace ImGui {
    static void DockingSpace() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags dock_flags = ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("MainDockSpace", nullptr, dock_flags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
}

// ==================== ASR UI State ====================
struct AsrUi {
    std::mutex  mtx;
    std::string history;
    std::string partial;
    std::string status;
    uint64_t    current_utterance = 0;
    bool        dirty = false;
};

static void drain_events(plugin_asr_stream* s, plugin::asr::fn::AsrPoll pfn_poll, AsrUi& ui) {
    std::vector<char> buf(65536);
    plugin_asr_result r{};
    r.struct_size = sizeof(r);

    for (int iter = 0; iter < 256; ++iter) {
        int32_t st = pfn_poll(s, &r, buf.data(), (uint32_t)buf.size());

        if (st == PLUGIN_ASR_EMPTY || st == PLUGIN_ASR_OK) break;

        if (st == PLUGIN_ASR_BUFFER_SMALL) {
            if (r.text_bytes + 1 > buf.size()) buf.resize(r.text_bytes + 1);
            continue;
        }

        if (st != PLUGIN_ASR_EVENT) {
            std::lock_guard lk(ui.mtx);
            ui.status = "ASR Poll Error: " + std::to_string(st);
            ui.dirty = true;
            break;
        }

        std::lock_guard lk(ui.mtx);
        switch (r.type) {
        case PLUGIN_ASR_PARTIAL:
            if (r.utterance_id != ui.current_utterance) {
                if (!ui.partial.empty()) {
                    ui.history += ui.partial;
                    if (!ui.history.empty() && ui.history.back() != '\n') ui.history += '\n';
                }
                ui.current_utterance = r.utterance_id;
                ui.partial.clear();
            }
            ui.partial = buf.data();
            ui.dirty = true;
            break;

        case PLUGIN_ASR_FINAL: {
            std::string t = buf.data();
            if (!t.empty()) {
                ui.history += t;
                if (!ui.history.empty() && ui.history.back() != '\n') ui.history += '\n';
            }
            if (r.utterance_id == ui.current_utterance) ui.partial.clear();
            ui.dirty = true;
            break;
        }

        case PLUGIN_ASR_GAP:
            ui.status = "ASR Audio Overflow. Some data dropped.";
            ui.dirty = true;
            break;

        case PLUGIN_ASR_ERROR:
            ui.status = "ASR Error: ";
            ui.status += buf.data();
            ui.dirty = true;
            break;

        case PLUGIN_ASR_END:
            break;
        }
    }
}

// ==================== Main ====================
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    timeBeginPeriod(1);
#endif

    gl::GlfwInitGuard glfwInitGuard;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto window = std::make_unique<gl::Window>(
        1200, 600, glm::ivec2{ 200, 100 }, "FlexMeet-client");
    glfwMakeContextCurrent(window->m_get);
    GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(
        glfwGetPrimaryMonitor());

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
    io->Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());

    ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);

    // ==================== Define Plug Struct ====================
    struct {
        struct {
            plugin::Loader loader{};
            plugin::asr::fn::AsrOpen   open{};
            plugin::asr::fn::AsrPush   push{};
            plugin::asr::fn::AsrPoll   poll{};
            plugin::asr::fn::AsrFinish finish{};
            plugin::asr::fn::AsrReset  reset{};
            plugin::asr::fn::AsrClose  close{};
        } asr;
    } plug;

    // ==================== ASR State & Thread Safety ====================
    std::mutex asr_mtx;
    plugin_asr_stream* asr_stream = nullptr;
    AsrUi ui;

    // ==================== 封装启用/停止函数 ====================
    auto toggle_asr = [&](bool enable) {
        std::lock_guard lk(asr_mtx);

        if (enable) {
            if (asr_stream) return; // 已经启用

            // 防止上一次异常残留未释放
            plug.asr.loader.free();

            // 1. 加载 DLL
            if (!plug.asr.loader.load("plugin/plugin_ASR.dll")) {
                std::lock_guard ui_lk(ui.mtx);
                ui.status = "ASR Load Failed: Cannot load DLL";
                return;
            }

            // 2. 获取函数指针
            bool got_funcs =
                plug.asr.loader.get(plug.asr.open, plugin::asr::name::asrOpen) &&
                plug.asr.loader.get(plug.asr.push, plugin::asr::name::asrPush) &&
                plug.asr.loader.get(plug.asr.poll, plugin::asr::name::asrPoll) &&
                plug.asr.loader.get(plug.asr.finish, plugin::asr::name::asrFinish) &&
                plug.asr.loader.get(plug.asr.reset, plugin::asr::name::asrReset) &&
                plug.asr.loader.get(plug.asr.close, plugin::asr::name::asrClose);

            if (!got_funcs) {
                plug.asr.loader.free();
                std::lock_guard ui_lk(ui.mtx);
                ui.status = "ASR Load Failed: Missing functions";
                return;
            }

            // 3. 打开 ASR 流
            char error[512] = {};
            int32_t rc = plug.asr.open("plugin", 0, &asr_stream, error, sizeof(error));
            if (rc != PLUGIN_ASR_OK || !asr_stream) {
                asr_stream = nullptr;
                plug.asr.loader.free(); // 失败必须卸载
                std::lock_guard ui_lk(ui.mtx);
                ui.status = "ASR open failed: " + std::string(error);
                return;
            }

            // 4. 启用成功
            std::lock_guard ui_lk(ui.mtx);
            ui.status = "ASR Started";
        }
        else {
            // 停止逻辑（极速强杀，不等待尾巴）
            if (!asr_stream && !plug.asr.close) return;

            // 1. 提取指针并立刻将全局状态置空
            auto* stream_to_close = asr_stream;
            auto close_fn = plug.asr.close;

            asr_stream = nullptr;
            plug.asr.open = nullptr;
            plug.asr.push = nullptr;
            plug.asr.poll = nullptr;
            plug.asr.finish = nullptr;
            plug.asr.reset = nullptr;
            plug.asr.close = nullptr;

            // 2. 直接强杀对象（底层 abort_callback 会瞬间打断推理并释放内存）
            if (stream_to_close) {
                try {
                    if (close_fn) {
                        close_fn(stream_to_close);
                    }
                }
                catch (...) {}
            }

            // 3. 彻底卸载 DLL（绝对不泄漏）
            plug.asr.loader.free();

            // 4. 清理 UI
            std::lock_guard ui_lk(ui.mtx);
            ui.partial.clear();
            ui.status = "ASR Stopped";
        }
        };

    // ==================== Audio Device ====================
    audio::User audioUser{ audio::sampleRate, audio::channels,
                           audio::periodSizeInFrames, audio::bufSec };
    audio::Player audioPlayer{ audio::sampleRate, audio::channels,
                               audio::periodSizeInFrames, &audioUser,
                               audio::User::callback };
    audio::Capture ad_cpt{ audio::sampleRate, audio::channels,
                           audio::periodSizeInFrames, audio::bufSec };
    audioPlayer.start();
    ad_cpt.Start();

    std::vector<float> audioFrames;
    std::atomic<bool>  play_audio{ false };

    // ==================== Capture Thread ====================
    std::jthread audioThread([&]() {
        auto last_time = std::chrono::steady_clock::now();

        while (!window->shouldClose()) {
            const auto target_interval = std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                    std::chrono::duration<boost::float64_t>(
                        1.0 / (audio::sampleRate / audio::periodSizeInFrames)));

            ad_cpt.giveFrames(audioFrames);

            if (play_audio) {
                audioUser.pushFrames(audioFrames.data(), audioFrames.size());
            }

            if (!audioFrames.empty()) {
                // 清理异常数据，防止 NaN 破坏底层模型
                for (float& f : audioFrames) {
                    if (!std::isfinite(f) || std::abs(f) > 1.0f) {
                        f = 0.0f;
                    }
                }

                std::lock_guard lk(asr_mtx);
                if (asr_stream && plug.asr.push) {
                    int32_t r = plug.asr.push(asr_stream, audioFrames.data(),
                        static_cast<uint32_t>(audioFrames.size()));

                    if (r == PLUGIN_ASR_INVALID) {
                        std::lock_guard ui_lk(ui.mtx);
                        ui.status = "Push INVALID (audio data rejected)";
                    }
                }
            }

            auto next_frame = last_time + target_interval;
            std::this_thread::sleep_until(next_frame);
            last_time = next_frame;
        }
        });

    // ==================== Main Loop ====================
    while (!window->shouldClose()) {
        glfwPollEvents();

        if (asr_stream) {
            std::lock_guard lk(asr_mtx);
            if (asr_stream && plug.asr.poll) {
                drain_events(asr_stream, plug.asr.poll, ui);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui::DockingSpace();

        ImGui::Begin("asr_test");
        if (ImGui::Button(play_audio ? "Stop Play" : "Play")) {
            play_audio = !play_audio;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("(Blue: Partial / White: Final)");

        ImGui::Separator();

        // 简洁的按钮逻辑
        bool is_active = (asr_stream != nullptr);
        if (ImGui::Button(is_active ? "Stop ASR (Unload Plugin)" : "Start ASR (Load Plugin)")) {
            toggle_asr(!is_active);
        }

        ImGui::SameLine();
        ImGui::TextColored(is_active ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
            is_active ? "Active" : "Inactive");

        ImGui::Separator();

        ImGui::BeginChild("ASRResult", ImVec2(0, 350), true);
        {
            std::lock_guard lk(ui.mtx);
            ImGui::PushTextWrapPos(0.0f);

            if (!ui.history.empty()) {
                ImGui::TextUnformatted(ui.history.c_str());
            }
            if (!ui.partial.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
                ImGui::TextUnformatted(ui.partial.c_str());
                ImGui::PopStyleColor();
            }
            if (!ui.status.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
                ImGui::TextUnformatted(ui.status.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();

            if (ui.dirty) {
                const float scroll_y = ImGui::GetScrollY();
                const float scroll_max_y = ImGui::GetScrollMaxY();
                if (scroll_y >= scroll_max_y - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ui.dirty = false;
            }
        }
        ImGui::EndChild();

        ImGui::End();
        ImGui::End();

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

    // ==================== Cleanup ====================
    audioThread.join();

    // 退出时直接调用封装好的函数，防止遗漏
    toggle_asr(false);

    ImGui::DestroyPlatformWindows();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}