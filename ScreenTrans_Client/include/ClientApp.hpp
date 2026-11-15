// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define USE_IMGUI 1
#define USE_IMGUI_VIEWPORT 1
#define LISTEN_MYSELF 0
#define RECV_DATA_EX_MOVABLE 0

#ifndef NDEBUG
#ifdef LISTEN_MYSELF
#	undef LISTEN_MYSELF
#	define LISTEN_MYSELF 0
#endif // LISTEN_MYSELF
#endif

#if	USE_IMGUI
#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"
#	include "imgui_internal.h"
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
#include <audio/Compress.hpp>
#include <ScreenCapture.h>
#include <st_signals.h>
#include <Room.h>

#include <util/util.hpp>
#include <util/Timer.hpp>
#include <imgui_ex.hpp>

#include <plugin/Loader.hpp>
#include <plugin/Asr.hpp>
#include <plugin/translator.hpp>

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <algorithm>
//#include <unordered_map>
#include <queue>
#include <vector>
#include <chrono>
#include <semaphore>
#include <format>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""s;

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/cstdfloat.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

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
#include <CameraCapture.hpp>

#include <Mp4Recorder.hpp>

//#ifndef NDEBUG
#	define print(x) std::cout<< x
//#else
//#	define print(x)
//#endif

#define println(x) print(x << "\n")
#define PL println(__LINE__)
#define loop for(;;)

#undef min
#undef max

using ST::H264Encoder, ST::H264Decoder, ST::ScreenCapture;

inline constexpr int SLEEP_TIME = 0;
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

namespace record {
	struct Frame {
		std::vector<std::shared_ptr<ST::DecodedFrame>> images;
		int imageWidth;
		int imageHeight;
		int64_t videoPtsUs;
		std::vector<float> audio;
		int channels;
		int sampleRate;
		int64_t audioPtsUs;

		void restore() {
			channels = audio::channels;
			sampleRate = audio::sampleRate;
			videoPtsUs = -1;
			audioPtsUs = -1;
			images.clear();
			audio.clear();
		}
	};
}

inline constexpr size_t words_msg_max_count = 128;

template <typename CB>
[[nodiscard]] std::size_t cbPop(CB& cb, typename CB::pointer out, std::size_t count);

class ClientApp {

	template<class A, class B>
	using UnorderedMap = boost::unordered_flat_map<A, B>;

	template<class A>
	using UnorderedSet = boost::unordered_flat_set<A>;

private:
	bool cam_ok{ false };
	std::counting_semaphore<> cam_sync0{ 0 };
	std::counting_semaphore<> cam_sync1{ 0 };
	int m_cam_w;
	int m_cam_h;
	CameraCapture m_cameraCapture;
	std::mutex mtx_camera;
	template<bool enable>
	void EnableCamera();
	void CameraCaptureThread();
	void CameraCaptureEncode();
	void CameraDecode();
	static inline constexpr size_t send_camera_packets_count = 4;
	static inline constexpr size_t recv_camera_packets_count = 4;
	static inline constexpr size_t total_capture_frames_count = 4;
	static inline constexpr size_t total_camera_frames_count = 4;
	//std::atomic<int32_t> rv_cam_orig_w{ 1000 };
	//std::atomic<int32_t> rv_cam_orig_h{ 800 };
	boost::lockfree::spsc_queue<std::vector<uint8_t>> send_camera_packets{ send_camera_packets_count };
	//boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_camera_packets{ recv_camera_packets_count };
	boost::lockfree::spsc_queue<ST::DecodedFrame> total_capture_frames{ total_capture_frames_count }; // to be encoded
	//boost::lockfree::spsc_queue<ST::DecodedFrame> total_camera_frames{ 4 }; // got from others


private:
	static std::unique_ptr<gl::Window> makeWindow();
private:
	template<bool lock_users>
	[[nodiscard]] bool isHost() {
		if constexpr (lock_users) {
			std::lock_guard lock(mtx_users);
			return get(users[get(logger.id)].is_host);
		}
		else {
			return get(users[get(logger.id)].is_host);
		}
	}
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
	void videoRecord();
	void audioMix();
private:
	void initVideoRecord();
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
		//std::counting_semaphore<> wait_threads{0};
	}p_show;
	Page Show();
	void initBeforeShow();
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
	std::mutex mtx_users; // mtx_chosen_users mtx_users
	std::mutex mtx_audioFramesLong;
	std::mutex mtx_speechWords;

	static inline constexpr size_t send_screen_packets_count = 4;
	static inline constexpr size_t recv_screen_packets_count = 4;
	//std::atomic<int32_t> rv_sc_orig_w{ 1920 };
	//std::atomic<int32_t> rv_sc_orig_h{ 1080 };
	boost::lockfree::spsc_queue<std::vector<uint8_t>> send_screen_packets{ send_screen_packets_count };
	//boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_screen_packets{ recv_screen_packets_count };
	//boost::lockfree::spsc_queue<std::shared_ptr<ST::DecodedFrame>> total_video_frames_v1{ 4 };

	//boost::lockfree::spsc_queue<ST::DecodedFrame> local_camera_frames{ 4 };
	std::atomic<bool> close_signal = false;
	struct User {
		std::atomic<bool> is_host{ false };
		std::atomic<bool> share_video{ false };
		std::atomic<bool> share_audio{ false };
		std::atomic<bool> share_camera{ false };
		static constexpr float audio_playing_least = 0.01f;
		std::atomic<bool> audio_playing{ false };
		std::string name;
		boost::circular_buffer<float> audioBuf{ static_cast<size_t>(audio::sampleRate * audio::channels * audio::bufSec) };
		struct {
			boost::circular_buffer<std::string> buf{ words_msg_max_count };
		} total_words_msg;

		User() = default;

		User(const User&) = delete;
		User& operator=(const User&) = delete;

		User(User&& other) noexcept
			: is_host(other.is_host.exchange(false, std::memory_order_relaxed)),
			share_video(other.share_video.exchange(false, std::memory_order_relaxed)),
			share_audio(other.share_audio.exchange(false, std::memory_order_relaxed)),
			share_camera(other.share_camera.exchange(false, std::memory_order_relaxed)),
			audio_playing(other.audio_playing.exchange(false, std::memory_order_relaxed)),
			name(std::move(other.name)),
			audioBuf(std::move(other.audioBuf)),
			total_words_msg{ std::move(other.total_words_msg.buf) }
		{
		}

		User& operator=(User&& other) noexcept {
			if (this == &other) return *this;

			is_host.store(other.is_host.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
			share_video.store(other.share_video.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
			share_audio.store(other.share_audio.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
			share_camera.store(other.share_camera.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
			audio_playing.store(other.audio_playing.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);

			name = std::move(other.name);
			audioBuf = std::move(other.audioBuf);
			total_words_msg.buf = std::move(other.total_words_msg.buf);
			return *this;
		}
	};	
	UnorderedMap < user::id_t, User > users; // init with self
	std::string speechWords;


	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	audio::Compressor audioCompressor{ audio::sampleRate, audio::channels };

	net::tcp::Client client{ net::tcp::Ip::v4 };
	net::tcp::Client client_audio{ net::tcp::Ip::v4 };

	using bool8_t = std::conditional_t<sizeof(bool) == 1, bool, uint8_t>;


	std::shared_mutex mtx_videoRecord;
	record::Mp4Recorder videoRecorder{};
	int videoRecordFrameRecording{ 0 };
	std::array<record::Frame, 2> videoRecordFrame;
	util::Timer videoRecordTimer;
	std::atomic<int64_t> recordAudioSamples;

	struct Ui {
		P1 p1;
		P2 p2;
		P3 p3;
		struct {
			ALIGN(bool8_t) share_audio {};
			ALIGN(bool8_t) share_video {};
			ALIGN(bool8_t) share_camera {};
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
		struct {
			struct {
				ALIGN(int32_t) fps {};
				ALIGN(int32_t) bitrate {};
				ALIGN(int32_t) width {};
				ALIGN(int32_t) height {};
			} encode;
		} camera;
		struct RoomUser {
			struct {
				ALIGN(uint32_t) set_room_passwd{};
				ALIGN(bool8_t) mute_all_keep{};
			} host;
		} room_user;
	};

	struct RecvDataEx {

#if RECV_DATA_EX_MOVABLE
		std::unique_ptr<boost::lockfree::spsc_queue<std::vector<uint8_t>>> recv_camera_packets
			= std::make_unique<boost::lockfree::spsc_queue<std::vector<uint8_t>>>(recv_camera_packets_count);
		std::unique_ptr<boost::lockfree::spsc_queue<std::vector<uint8_t>>> recv_screen_packets
			= std::make_unique<boost::lockfree::spsc_queue<std::vector<uint8_t>>>(recv_screen_packets_count);

		std::unique_ptr<boost::lockfree::spsc_queue<ST::DecodedFrame>> total_camera_frames =
			std::make_unique<boost::lockfree::spsc_queue<ST::DecodedFrame>>(total_camera_frames_count);
		std::unique_ptr<boost::lockfree::spsc_queue<std::shared_ptr<ST::DecodedFrame>>> total_video_frames_v1 = 
			std::make_unique<boost::lockfree::spsc_queue<std::shared_ptr<ST::DecodedFrame>>>(4);

		RecvDataEx(const RecvDataEx&) = delete;
		RecvDataEx& operator=(const RecvDataEx&) = delete;
		RecvDataEx(RecvDataEx&&) noexcept = default;
		RecvDataEx& operator=(RecvDataEx&&) noexcept = default;
#else
		boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_camera_packets{ recv_camera_packets_count };
		boost::lockfree::spsc_queue<std::vector<uint8_t>> recv_screen_packets{ recv_screen_packets_count };

		boost::lockfree::spsc_queue<ST::DecodedFrame> total_camera_frames{ total_camera_frames_count };
		boost::lockfree::spsc_queue<std::shared_ptr<ST::DecodedFrame>> total_video_frames_v1{ 4 };

		RecvDataEx(const RecvDataEx&) = delete;
		RecvDataEx& operator=(const RecvDataEx&) = delete;
		RecvDataEx(RecvDataEx&&) noexcept = delete;
		RecvDataEx& operator=(RecvDataEx&&) noexcept = delete;
#endif
		const user::id_t user;
		H264Decoder cameraDecoder{};
		H264Decoder videoDecoder{};

		std::atomic<int32_t> rv_sc_orig_w{ 1920 };
		std::atomic<int32_t> rv_sc_orig_h{ 1080 };
		std::atomic<int32_t> rv_cam_orig_w{ 1000 };
		std::atomic<int32_t> rv_cam_orig_h{ 800 };

		// only used for show
		//gl::Texture2D img{ 1, 1,{ 0x33,0x33,0x33,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4 };
		//gl::Texture2D camTexture{ 1, 1,{ 0,0,0,0 }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4 };
		std::vector<uint8_t> camPixelBuffer;
		ST::DecodedFrame cam_frame;
		std::shared_ptr<ST::DecodedFrame> decodedFrame;

		RecvDataEx(user::id_t user0) : user(user0) {}
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
				std::atomic<int> fps = 30;
				std::atomic<int> bitrate = 4'000'000;
				std::atomic<int> width{0};
				std::atomic<int> height{0};
				std::counting_semaphore<> wait_wh{0};
			} encode;
		} video;
		struct {
			struct {
				//std::mutex mtx;
				std::atomic<int> fps = 30;
				std::atomic<int> bitrate = 4'000'000;
				std::atomic<int> width{ 0 };
				std::atomic<int> height{ 0 };
				std::counting_semaphore<> wait_wh{ 0 };
			} encode;
		} camera;
		struct RoomUser{
			std::atomic<bool> is_kicked_off{ false };
			std::atomic<bool> is_muted_keep{ false };
			std::mutex mtx_chosen_users; // mtx_chosen_users mtx_users
			UnorderedMap<user::id_t, std::shared_ptr<RecvDataEx>> chosen_users; // init with self, share_ptr: cuz may destruct while "decoding"
			std::atomic<user::id_t> record_user{ user::invalid_id };
			std::atomic<bool> record_video{ false };
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

	struct TranslateDirection {
		std::string src{};
		std::string tar{};
		std::string model_dir{};
		bool operator==(const TranslateDirection& that) const {
			return this->src == that.src &&
				this->tar == that.tar&&
				this->model_dir == that.model_dir;
		}
		bool operator!=(const TranslateDirection& that) const {
			return !(*this == that);
		}
	};

	struct {
		struct {
			plugin::Loader loader{};
			plugin::asr::fn::AsrOpen open{};
			plugin::asr::fn::AsrPush push{};
			plugin::asr::fn::AsrPoll poll{};
			plugin::asr::fn::AsrFinish finish{};
			plugin::asr::fn::AsrReset reset{};
			plugin::asr::fn::AsrClose close{};
			std::mutex mtx;
			plugin_asr_stream* stream = nullptr;
			plugin::asr::AsrUi ui;

			std::string dir{};
		} asr;

		struct {
			using Direction = ClientApp::TranslateDirection;

			std::string dir{};
			std::mutex mtx;
			plugin::Loader loader{};
			plugin::translator::fn::Register Registor{};
			plugin::translator::fn::unload unload{};
			plugin::translator::fn::Translate translate{};
			plugin::translator::fn::Close close{};
			std::vector<Direction> directions;
			// ui
			std::mutex mtx_ui;
			std::string error;
			std::string toBeTranslated;
			std::string result;
			Direction direction{};
		} translator;
	} plug;

	template<bool mtx>
	bool asrOn();
	void asrLoad();
	void asrDownload();
	template<bool mtx>
	void asrPushData(const float* audioFrames, size_t count);
	//template<bool mtx>
	//void asrSetStatus(std::string_view status) {
	//	if constexpr (mtx) {
	//		std::lock_guard lock{ plug.asr.ui.mtx };
	//		if (plug.asr.ui.status.empty()) { plug.asr.ui.status = status; }
	//	}
	//	else {
	//		if (plug.asr.ui.status.empty()) { plug.asr.ui.status = status; }
	//	}
	//}

	template<bool mtx>
	bool translatorOn();
	void translatorLoad();
	void translatorDownload();
	void translatorLoadDirections();
	template<bool mtx>
	void translatorSetError(std::string_view error) { 
		if constexpr (mtx) {
			std::lock_guard lock{ plug.translator.mtx_ui };
			if (plug.translator.error.empty()) { plug.translator.error = error; }
		}
		else {
			if (plug.translator.error.empty()) { plug.translator.error = error; }
		}
	}

	void translateThread();

public:
	ClientApp();
	~ClientApp();
	void run();
};