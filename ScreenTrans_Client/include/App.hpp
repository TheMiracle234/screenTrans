// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <Client.h>
#include <Socket.h>
#include <H264Encoder.h>
#include <H264Decoder.h>
#include <AudioCapture.h>
#include <audio/Player.hpp>
#include <audio/User.hpp>
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

#include <boost/lockfree/spsc_queue.hpp>

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

namespace audio {
	inline constexpr uint32_t sampleRate = 48000;
	inline constexpr uint32_t channels = 2;
	inline constexpr uint32_t periodSizeInFrames = 960;
	static_assert(sampleRate% periodSizeInFrames == 0);
	inline constexpr double periodSec = static_cast<double>(periodSizeInFrames) / sampleRate;
	inline constexpr int mixPeriods = 5;
	inline constexpr int bufSec = 1;
}

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
	void audioMix();
private:
	void pageRenderBegin(const char* title, int sleepTime);
	void pageRenderEnd();
	struct PageRenderGuard {
		App* p;
		PageRenderGuard(App* self, const char* title, int sleepTime) : p{self} { p->pageRenderBegin(title, sleepTime); }
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
	struct User{
		SOCKET skt{ INVALID_SOCKET };
		boost::lockfree::spsc_queue<float> audioBuf{ audio::sampleRate * audio::channels * audio::bufSec };
	};
	std::unordered_map < std::string, User > users; // init with self
	//boost::lockfree::spsc_queue<float> audioBufGlobal{ audio::sampleRate * audio::channels * audio::bufSec };
	std::atomic<double> max_fps_data = 20;
	std::atomic<double> max_fps_video = 20;

	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, 1 };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	TM::Client client{ Socket::TCP, Socket::IPV4 };
public:
	App();
	~App();
	void run();
};