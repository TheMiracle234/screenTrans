// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <Client.h>
#include <Socket.h>
#include <H264Encoder.h>
#include <H264Decoder.h>
#include <AudioPlay.h>
#include <AudioCapture.h>
#include <ScreenCapture.h>
#include <st_signals.h>
#include <Room.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <gl/Window.h>
#include <gl/Shader.h>
#include <gl/VertexArray.h>
#include <gl/VertexBuffer.h>
#include <gl/IndexBuffer.h>
#include <gl/Render.h>
#include <gl/Texture.h>

#define USE_IMGUI 1

#if	USE_IMGUI
#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"

#	if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#		pragma comment(lib, "legacy_stdio_definitions")
#	endif
#endif

#ifndef NDEBUG
#	define print(x) std::cout<< x
#else
#	define print(x)
#endif

#define println(x) print(x << "\n")
#define PL println(__LINE__)
#define loop for(;;)

#undef min
#undef max

using TM::Client, TM::Socket;
using ST::H264Encoder, ST::H264Decoder, ST::AudioPlay, ST::AudioCapture, ST::ScreenCapture;

inline constexpr int SLEEP_TIME = 0;
inline constexpr size_t MAX_VIDEO_FRAMES = 10;
inline constexpr size_t KEEP_FRAMES = 4;
inline constexpr double MAX_MAX_FPS = 150.0;
inline constexpr double MIN_MAX_FPS = 5.0;

#if USE_IMGUI
namespace ImGui {
	void DockingSpace();
}
#endif

class App {
private:
	//inline std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }
	//inline double duration_to_double(std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); }	
	static std::unique_ptr<gl::Window> makeWindow();
private:
	/* send: signals, id (, name, audio_frames(, pk_size, pk[n])) */
	void Receive();
	/* recv: id, name, audio_frames, choose_socket, pk_size, pk[n] */
	void Send();
private:
	void pageRenderBegin(const char* title);
	void pageRenderEnd();
	struct PageRenderGuard {
		App* p;
		PageRenderGuard(App* self, const char* title) : p{self} { p->pageRenderBegin(title); }
		~PageRenderGuard() { p->pageRenderEnd(); }
	};
	friend struct PageRenderGuard;
	enum class Page {
		connectToServer,
		chooseMode,
		makeRoom,
		enterRoom,
		Show,
		serverStatusError,
		never,
		//count,
	};
	Page connectToServer();
	Page chooseMode();
	Page makeRoom();
	Page enterRoom();
	Page serverStatusError();
	Page Show();
public:
	gl::GlfwInitGuard glfwInitGuard;
	std::unique_ptr<gl::Window> window{ makeWindow()};

#if USE_IMGUI
	float main_scale{};
	ImGuiIO* io{};
	ImGuiStyle* style{};
#endif

	struct {
		std::string name{};
		uint32_t room_id{ Room::invalid_id };
		uint32_t passwd{ Room::invalid_passwd };
	} logger;
	Page page;
	std::mutex mtx_video_frames;
	std::mutex mtx_close;
	std::mutex mtx_users;
	std::mutex mtx_choiceChange;
	std::queue<ST::DecodedFrame> total_video_frames;
	std::atomic<bool> close_signal = false;
	std::string chosen_user; // init with self
	std::unordered_map<std::string, SOCKET> users; // init with self

	std::atomic<double> max_fps_data = 20;
	std::atomic<double> max_fps_video = 20;

	AudioPlay ad_player{ 48000, 1, 256 };
	TM::Client client{ Socket::TCP, Socket::IPV4 };
public:
	App();
	~App();
	void run();
};