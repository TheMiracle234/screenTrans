// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define USE_IMGUI 1

#if	USE_IMGUI
#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"

#	if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#		pragma comment(lib, "legacy_stdio_definitions")
#	endif
#endif

#include <net/tcp/Client.hpp>
#include <net/Socket.hpp>
#include <H264Encoder.h>
#include <H264Decoder.h>
#include <audio/Capture.hpp>
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
#include <queue>
#include <chrono>

#include <boost/lockfree/spsc_queue.hpp>

#include <gl/Window.h>
#include <gl/Shader.h>
#include <gl/VertexArray.h>
#include <gl/VertexBuffer.h>
#include <gl/IndexBuffer.h>
#include <gl/Render.h>
#include <gl/Texture.h>

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

using ST::H264Encoder, ST::H264Decoder, ST::ScreenCapture;

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
	enum class Page : uint8_t{
		connectToServer,
		chooseMode,
		makeRoom,
		enterRoom,
		Show,
		serverStatusError,
		never,
		//count,
	};
	struct P1 {
		static inline constexpr size_t buf_size = 256;
		char ipv4[buf_size]{};
		static inline constexpr uint32_t invalid_port{ 0 };
		uint32_t port{ invalid_port };
		char name[buf_size]{};
	}p1;
	Page connectToServer();

	Page chooseMode();
	
	struct P2 {
		uint32_t passwd{ Room::invalid_passwd };
	}p2;
	Page makeRoom();

	struct P3 {
		uint32_t room_id{ Room::invalid_id };
		uint32_t passwd{ Room::invalid_passwd };
	}p3;
	Page enterRoom();
	
	Page serverStatusError();
	
	Page Show();

public:
	net::InitGuard initGuard{};
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
	Page page{ Page::connectToServer };
	
	std::mutex mtx_video_frames;
	std::mutex mtx_close;
	std::mutex mtx_users;

	std::queue<ST::DecodedFrame> total_video_frames;
	std::atomic<bool> close_signal = false;
	std::atomic<net::socket_t> chosen_user; // init with self
	struct User{
		std::string name;
		boost::lockfree::spsc_queue<float> audioBuf{ audio::sampleRate * audio::channels * audio::bufSec };
	};
	std::unordered_map < net::socket_t, User > users; // init with self
	std::atomic<double> max_fps_data = 20;
	std::atomic<double> max_fps_video = 20;

	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	net::tcp::Client client{ net::tcp::Ip::v4 };
public:
	App();
	~App();
	void run();
};