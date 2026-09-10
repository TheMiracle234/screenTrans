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
#	define println(x) std::cout<< x << "\n"
#	define print(x) std::cout<< x
#else
#	define println(x)
#	define print(x)
#endif
#define loop for(;;)

#undef min
#undef max

using TM::Client, TM::Socket;
using ST::H264Encoder, ST::H264Decoder, ST::AudioPlay, ST::AudioCapture, ST::ScreenCapture;

static inline constexpr int SLEEP_TIME = 0;
static inline constexpr size_t MAX_VIDEO_FRAMES = 10;
static inline constexpr size_t KEEP_FRAMES = 4;

#if USE_IMGUI
namespace ImGui {
	void DockingSpace();
}
#endif

class App {
private:
	//inline std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }
	//inline double duration_to_double(std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); }
	struct imgStruct {
		float pos[3];
		float texCoords[2];
	};
	
	inline static constexpr const char* vs = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 texCoords;
void main(){
	texCoords = aTexCoords;
	gl_Position = vec4(aPos.x, -aPos.y, aPos.z, 1.0f);
}
)";
	inline static constexpr const char* fs = R"(
#version 330 core
in vec2 texCoords;
uniform sampler2D sampler;
out vec4 FragColor;
void main(){
	FragColor = texture(sampler, texCoords);
}
)";

	static std::unique_ptr<gl::Window> makeWindow();

	/* send: signals, id (, name, audio_frames(, pk_size, pk[n])) */
	void Receive();
	/* recv: id, name, audio_frames, choose_socket, pk_size, pk[n] */
	void Send();
	void Show();
	void connectInput(char* ipv4, char* port, uint32_t& port_num, char* name, size_t buf_size, bool not_first);
	void register_handle();
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
		uint32_t room_id{};
		uint32_t passwd{};
	} logger;

	std::mutex mtx_video_frames;
	std::mutex mtx_close;
	std::mutex mtx_users;
	std::mutex mtx_choiceChange;
	std::queue<ST::DecodedFrame> total_video_frames;
	std::atomic<bool> close_signal = false;
	std::string chosen_user; // init with self
	std::unordered_map<std::string, SOCKET> users; // init with self

	std::atomic<double> max_fps_data = 128;
	std::atomic<double> max_fps_video = 128;

	AudioPlay ad_player{ 48000, 1, 256 };
	TM::Client client{ Socket::TCP, Socket::IPV4 };
public:
	App();
	~App();
	void run();
};