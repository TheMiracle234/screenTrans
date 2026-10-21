// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define USE_IMGUI 1
#define USE_IMGUI_VIEWPORT 1

#define USE_ASR 0

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
#include <util/util.hpp>

#include <plugin/Loader.hpp>
#include <plugin/Asr.hpp>

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <vector>
#include <chrono>
#include <semaphore>
using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""s;

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/cstdfloat.hpp>
#include <boost/circular_buffer.hpp>

#include <ccap.h>
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

#include <gl/Window.h>
#include <gl/Shader.h>
#include <gl/VertexArray.h>
#include <gl/VertexBuffer.h>
#include <gl/IndexBuffer.h>
#include <gl/Render.h>
#include <gl/Texture.h>
#include <gl/util.h>

#include <compile_time.hpp>
#include "CameraCapture.hpp"

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
//inline constexpr size_t MAX_VIDEO_FRAMES = 9;
//inline constexpr size_t KEEP_FRAMES = 3;
inline constexpr boost::float64_t MAX_MAX_FPS = 150.0;
inline constexpr boost::float64_t MIN_MAX_FPS = 5.0;
namespace video {
	inline constexpr int min_bitrate = 2'000'000;
	inline constexpr int max_bitrate = 20'000'000;
}

namespace audio {
	inline constexpr uint32_t sampleRate = 16'000;
	inline constexpr uint32_t channels = 1;
	inline constexpr uint32_t periodSizeInFrames = 160;
	static_assert(sampleRate% periodSizeInFrames == 0);
	inline constexpr boost::float64_t periodSec = static_cast<boost::float64_t>(periodSizeInFrames) / sampleRate;
	//inline constexpr int mixPeriods = 1;
	inline constexpr double bufSec = 1.0;
}

inline constexpr size_t words_msg_max_count = 128;

template <typename CB>
[[nodiscard]] std::size_t cbPop(CB& cb, typename CB::pointer out, std::size_t count);

class ClientApp {
private:
	CameraCapture m_cameraCapture;
	bool m_enableCamera = false;
	void EnableCamera(bool enable);
	void CameraCaptureThread();

	void CameraCaptureEncode();
	void CameraDecode();
	static inline constexpr size_t send_camera_packets_count = 4;
	static inline constexpr size_t recv_camera_packets_count = 4;
	static inline constexpr size_t total_capture_frames_count = 4;

	boost::lockfree::spsc_queue<std::vector<uint8_t>> send_camera_packets{ send_camera_packets_count };

	boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_camera_packets{ recv_camera_packets_count };

	boost::lockfree::spsc_queue<ST::DecodedFrame> total_capture_frames{ total_capture_frames_count };



private:
	static std::unique_ptr<gl::Window> makeWindow();
private:
	template<bool lock_users>
	[[nodiscard]] bool isHost();
	[[nodiscard]] bool clientClosed();
private:
	/* recv: signals, id (, name, is_host_from_server, is_host_from_client(, wanna_kick_off), audio_frames(, pk_size, pk[n])) */
	void Receive();
	/* send: id, name, is_host_from_client(, wanna_kick_off), audio_frames, pk_size, pk[n] */
	void Send();
	void audioSend();
	void audioRecv();
	void ScreenCaptureEncode();
	void ScreenDecode();
	void CameraCapture();
	void audioMix();
	void asrRecv();
private:
	void storeUiState();
	void loadUiState();
	void pageRenderBegin(const char* title, int sleepTime);
	void pageRenderEnd();
	struct PageRenderGuard {
		ClientApp* p;
		PageRenderGuard(ClientApp* self, const char* title, int sleepTime) : p{ self } { p->pageRenderBegin(title, sleepTime); }
		~PageRenderGuard() { p->pageRenderEnd(); }
	};
	friend struct PageRenderGuard;
	enum class Page {
		connectToServer,
		chooseMode,
		makeRoom,
		enterRoom,
		Show,
		Tmp,
		//count,
	};
	struct P1 {
		static inline constexpr uint16_t invalid_port{ 0 };
		ALIGN(char) ipv4[64]{};
		ALIGN(uint16_t) port{ invalid_port };
		ALIGN(char) name[128]{};
	}p1;
	Page connectToServer();

	Page chooseMode();

	struct P2 {
		ALIGN(uint32_t) passwd{ Room::invalid_passwd };
	}p2;
	Page makeRoom();

	struct P3 {
		ALIGN(uint32_t) room_id{ room::invalid_id };
		ALIGN(uint32_t) passwd{ Room::invalid_passwd };
	}p3;
	Page enterRoom();

	//Page serverStatusError();

	struct ShowPara{
	public:
		enum class End {
			clientClosed = 0,
			kickOff,
			Return,
			Default = clientClosed,
		};
		ShowPara() { restore(); }
		void setEnd(bool ended, End showEnd);
		void restore();
		bool ended() { return m_ended.load(std::memory_order_acquire); }
		End showEnd() { return m_showEnd; }
	private:		
		std::atomic<bool> m_ended{ false };
		End m_showEnd{ End::Default };
	public:
		std::counting_semaphore<> wait_threads{0};
	}p_show;
	Page Show();
	void restoreAfterShow();

	struct {
		std::string title{};
		std::string content{};
		Page back{Page::Tmp};
	}p_tmp;
	Page Tmp();
	Page makeTmp(std::string title, std::string content, Page back);
	Page makeTmpServerStatusError() {
		return makeTmp("server_status_error", "server status error. please reconnect or close the window", Page::connectToServer);
	}

private:
	net::InitGuard initGuard{};
	gl::GlfwInitGuard glfwInitGuard;
	std::unique_ptr<gl::Window> window{ makeWindow() };

#if USE_IMGUI
	float main_scale{};
	ImGuiIO* io{};
	ImGuiStyle* style{};
#endif

	struct {
		std::mutex mtx_name;
		std::string name{};
		std::atomic<user::id_t> id{ user::invalid_id };
		uint32_t room_id{ room::invalid_id };
		uint32_t passwd{ Room::invalid_passwd };
	} logger;
	Page page{ Page::connectToServer };

	//std::mutex mtx_video_frames;
	std::mutex mtx_close;
	std::mutex mtx_users;
	std::mutex mtx_audioFramesLong;
	std::mutex mtx_speechWords;

	static inline constexpr size_t send_screen_packets_count = 4;
	boost::lockfree::spsc_queue<std::vector<uint8_t>> send_screen_packets{ send_screen_packets_count };
	static inline constexpr size_t recv_screen_packets_count = 4;
	boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_screen_packets{ recv_screen_packets_count };
	boost::lockfree::spsc_queue<ST::DecodedFrame> total_video_frames_v1{ 4 };

	boost::lockfree::spsc_queue<ST::DecodedFrame> total_camera_frames{ 4 };
	boost::lockfree::spsc_queue<ST::DecodedFrame> local_camera_frames{ 4 };
	//std::queue<ST::DecodedFrame> total_video_frames;
	std::atomic<bool> close_signal = false;
	struct User {
		std::atomic<bool> is_host{ false };
		std::atomic<bool> share_video{ false };
		std::atomic<bool> share_audio{ false };
		std::atomic<bool> share_camera{ false };
		static constexpr float audio_playing_least = 0.01f;
		std::atomic<bool> audio_playing{ false };
		std::string name;
		//boost::lockfree::spsc_queue<float> audioBuf{ static_cast<size_t>(audio::sampleRate * audio::channels * audio::bufSec) };
		boost::circular_buffer<float> audioBuf{ static_cast<size_t>(audio::sampleRate * audio::channels * audio::bufSec) };
		struct {
			//std::mutex mtx;
			boost::circular_buffer<std::string> buf{ words_msg_max_count };
		} total_words_msg;
	};
	std::unordered_map < user::id_t, User > users; // init with self
	std::vector<float> audioFramesLong{};
	std::string speechWords;


	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	net::tcp::Client client{ net::tcp::Ip::v4 };
	net::tcp::Client client_audio{ net::tcp::Ip::v4 };

	using bool8_t = std::conditional_t<sizeof(bool) == 1, bool, uint8_t>;

	struct Ui {
		P1 p1;
		P2 p2;
		P3 p3;
		struct {
			ALIGN(bool8_t) share_audio {};
			ALIGN(bool8_t) share_video {};
			ALIGN(boost::float64_t) fps{20};
		} send;
		struct {
			ALIGN(boost::float64_t) fps{100};
			struct {
				ALIGN(int32_t) fps{};
				ALIGN(int32_t) bitrate{};
				ALIGN(int32_t) width{};
				ALIGN(int32_t) height{};
			} encode;
		} video;
		struct RoomUser {
			struct {
				ALIGN(uint32_t) set_room_passwd{};
				ALIGN(bool8_t) mute_all_keep{};
			} host;
		} room_user;
	};

	struct {
		struct {
			std::atomic<bool> share_audio{ false };
			std::atomic<bool> share_video{ false };
			std::atomic<bool> share_camera{ false };
			std::atomic<double> real_fps{};
			std::atomic<boost::float64_t> fps = 20;
			std::atomic<user::id_t> words_msg_target{ user::public_target }; // invalid => public msg
			struct {
				std::mutex mtx;
				boost::circular_buffer<std::string> buf{ words_msg_max_count };
			} total_words_msg;
			struct {
				char buf[words_msg_bytes]{};
				std::mutex mtx;
			} words_msg;
		} send;
		struct {
			std::atomic<boost::float64_t> fps = 64;
			struct {
				//std::mutex mtx;
				std::atomic<int> fps = 30;
				std::atomic<int> bitrate = 4'000'000;
				std::atomic<int> width{0};
				std::atomic<int> height{0};
				std::counting_semaphore<> wait_wh{0};
			} encode;
		} video;
		struct RoomUser{
			std::atomic<bool> is_kicked_off{ false };
			std::atomic<bool> is_muted_keep{ false };
			std::atomic<user::id_t> chosen_user; // init with self
			struct {
				std::atomic<user::id_t> wanna_kick_off{ user::invalid_id };
				std::atomic<user::id_t> transfer_host{ user::invalid_id };
				std::atomic<uint32_t> room_passwd{ Room::invalid_passwd };
				std::atomic<uint32_t> set_room_passwd{ Room::invalid_passwd };
				std::atomic<bool> mute_all_once{ false };
				std::atomic<bool> mute_all_keep{ false };
			} host;
		} room_user;
	} ui;

	void* any_usage{nullptr}; // now only for renderFunc in draw function
	
	struct {
		struct {
			plugin::Loader loader{};
			plugin::fn::AsrInit init{};
			plugin::fn::AsrTranscribe transcribe{};
			plugin::fn::AsrFree free{};
		} asr;
	} plug;
public:
	ClientApp();
	~ClientApp();
	void run();
};