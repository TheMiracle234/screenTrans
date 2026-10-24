#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <gl/Window.h>

#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"

#	if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#		pragma comment(lib, "legacy_stdio_definitions")
#	endif

#if defined(_WIN32)
#   define FONT_CJK_PATH "C:/Windows/Fonts/msyh.ttc"          // 微软雅黑
#elif defined(__APPLE__)
#   define FONT_CJK_PATH "/System/Library/Fonts/PingFang.ttc" // 苹方
#elif defined(__linux__)
#   define FONT_CJK_PATH "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
#else
static_assert(false, "unsupported platform font");
#endif

//#include <imgui_comp.hpp>

#ifdef _WIN32
#	include <Windows.h>
#	include <timeapi.h>
#	pragma comment(lib, "winmm.lib")
#endif


#include <audio/Capture.hpp>
#include <audio/Player.hpp>
#include <audio/User.hpp>

#include <plugin/asr.h>

#include <boost/cstdfloat.hpp>

namespace audio {
	inline constexpr uint32_t sampleRate = 16'000;
	inline constexpr uint32_t channels = 1;
	inline constexpr uint32_t periodSizeInFrames = 160;
	static_assert(sampleRate% periodSizeInFrames == 0);
	inline constexpr boost::float64_t periodSec = static_cast<boost::float64_t>(periodSizeInFrames) / sampleRate;
	//inline constexpr int mixPeriods = 1;
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
int main() {
#ifdef _WIN32
	timeBeginPeriod(1);   // ClientApp::App() 里，配合 ClientApp::~App() 的 timeEndPeriod(1)
#endif



	gl::GlfwInitGuard glfwInitGuard;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
////////////============
	//glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);           // 去除原生窗口边框（让 ImGui 接管标题栏）
	//glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE); // 开启透明缓冲，圆角外侧才会透明

	auto window = std::make_unique<gl::Window>(1200, 600, glm::ivec2{ 200, 100 }, "FlexMeet-client");
	glfwMakeContextCurrent(window->m_get);
	GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	auto* io = &ImGui::GetIO();
	io->ConfigFlags =
		ImGuiConfigFlags_ViewportsEnable |
		ImGuiConfigFlags_DockingEnable |
		ImGuiConfigFlags_NavEnableKeyboard |     // Enable Keyboard Controls
		ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	ImGui::StyleColorsDark();

	// Setup scaling
	auto* style = &ImGui::GetStyle();
	style->ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style->FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)
////////////============
	//style->WindowRounding = 10.0f;  // 窗口圆角
	//style->FrameRounding = 6.0f;   // 控件（按钮、输入框）圆角
	//style->ScrollbarRounding = 8.0f;   // 滚动条圆角
	//style->GrabRounding = 6.0f;   // 滑动条圆角
	//style->WindowBorderSize = 1.0f;   // 保留细边框
	//style->FrameBorderSize = 0.0f;   // 按钮不需要边框
	//style->WindowPadding = ImVec2(12, 12);

	ImFontConfig fontConfig{};

	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;

	//io.Fonts->Clear();
	//io->Fonts->AddFontFromFileTTF("font/MapleMonoNL-CN-Regular.ttf", 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	io->Fonts->AddFontFromFileTTF(FONT_CJK_PATH, 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	//io.Fonts->Build();
	//io.Fonts->AddFontFromFileTTF("font/msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f); // 窗口内部的背景色，Alpha 必须为 1
	
	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audioPlayer.start();
	ad_cpt.Start();
	std::vector<float> audioFrames{}; // buffer to get audio data

	std::atomic<bool> play_audio{};


	std::jthread audioThread([&]() {
		auto last_time = std::chrono::steady_clock::now();
		while (!window->shouldClose()) {
			const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<boost::float64_t>(1.0 / (audio::sampleRate / audio::periodSizeInFrames))
			);

			ad_cpt.giveFrames(audioFrames);
			if (play_audio) {
				audioUser.pushFrames(audioFrames.data(), audioFrames.size());
			}

			auto next_frame = last_time + target_interval;
			std::this_thread::sleep_until(next_frame);
			last_time = next_frame;
		}
		});


	while (!window->shouldClose()) {

		glfwPollEvents();
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);




		ImGui::DockingSpace();
		ImGui::Begin("asr_test");
		if (ImGui::Button(play_audio ? "stop_play_audio" : "play_audio")) {
			play_audio = !play_audio;
		}


		ImGui::End();
		ImGui::End();//docking end
		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		{
			GLFWwindow* backup = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup);   // 恢复主上下文
		}
		window->swapBuffers();
	}



	// Cleanup
	ImGui::DestroyPlatformWindows();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

#ifdef _WIN32
	timeEndPeriod(1);   // ClientApp::App() 里，配合 ClientApp::~App() 的 timeEndPeriod(1)
#endif

}