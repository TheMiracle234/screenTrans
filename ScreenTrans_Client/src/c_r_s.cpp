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

#include <window/Window.h>
#include <component/Image.h>
#include <component/Button.h>


#define USE_IMGUI 1

#if	USE_IMGUI
#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"

#	if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#		pragma comment(lib, "legacy_stdio_definitions")
#	endif
#endif


#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
#define loop for(;;)

using TM::Window, TM::Image;
using TM::Client, TM::Socket;
using ST::H264Encoder, ST::H264Decoder, ST::AudioPlay, ST::AudioCapture, ST::ScreenCapture;

struct {
	std::string name;
	uint32_t room_id;
	uint32_t passwd;
} logger;

constexpr int SLEEP_TIME = 5;
constexpr size_t MAX_VIDEO_FRAMES = 10;
constexpr size_t KEEP_FRAMES = 4;

std::mutex mtx_video_frames;
std::mutex mtx_close;
std::mutex mtx_users;
std::mutex mtx_choiceChange;
std::queue<ST::DecodedFrame> total_video_frames;
std::atomic<bool> close_signal = false;
std::string chosen_user; // init with self
std::unordered_map<std::string, SOCKET> users; // init with self

std::atomic<double> max_fps_data = 100.0f;
std::atomic<double> max_fps_video = 100.0f;

AudioPlay ad_player(48000, 1, 256);
TM::Client client(Socket::TCP, Socket::IPV4);

namespace ImGui {
	void DockingSpace() {
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		static constexpr ImGuiWindowFlags dock_flags = ImGuiWindowFlags_NoDocking |
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

//inline std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }
//inline double duration_to_double(std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); }

/*
send: signals, id (, name (, audio_frames, pk_size, pk[n]))
*/
void Receive() {
	H264Decoder decoder;

	ad_player.Start();

	auto last_time = std::chrono::steady_clock::now();

	while(!close_signal) {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / max_fps_data.load(std::memory_order_acquire))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		{
			std::lock_guard lock(mtx_close);
			if (client.Closed())
				break;
		}

		// if closed, remove it and reset choice
		auto signals = client.ReceiveParseTo<Signal>();
		if (*signals & signal_closed) {
			auto id = client.ReceiveParseTo<SOCKET>();
			{
				std::lock_guard lock(mtx_users);
				auto count = std::erase_if(users, [&](auto& user) { return user.second == *id;});
			}
			{
				std::lock_guard lock(mtx_choiceChange);
				chosen_user = logger.name;
			}
			continue;
		}


		auto id = client.ReceiveParseTo<SOCKET>();
		auto other_name = client.ReceiveString();
		{
			std::lock_guard lock(mtx_users);
			users[*other_name] = *id;
		}

		if (*signals & signal_choiceNotMatch) {
			continue;
		}

		auto frames = client.ReceiveVec<int16_t>();
		ad_player.PushFrames(*frames);

		// each packet
		auto pk_size = client.ReceiveParseTo<int32_t>();
		for (int i = 0;i < *pk_size;++i) {
			auto pk = client.Receive();
			auto frames = decoder.DecodePacket(*pk);
			if (!frames.empty()) {
				std::lock_guard<std::mutex> lock(mtx_video_frames);
				size_t current_size = total_video_frames.size();
				size_t new_count = frames.size();

				if (current_size + new_count > MAX_VIDEO_FRAMES) {
					while (!total_video_frames.empty())
						total_video_frames.pop();

					size_t keep = std::min(KEEP_FRAMES, new_count);
					auto start = frames.end() - keep;
					for (auto it = start; it != frames.end(); ++it) {
						total_video_frames.push(std::move(*it));
					}
				}
				else {
					for (auto& frame : frames) {
						total_video_frames.push(std::move(frame));
					}
				}
			}
		}

	}
	{
		std::lock_guard lock(mtx_close);
		client.Close();
	}
}

/*
recv: id, name, choose_socket, audio_frames, pk_size, pk[n]
*/
void Send() {
	ScreenCapture capture;
	if (!capture.Initialize()) {
		std::cout << "capture init failed\n";
		exit(1);
	}

	while (!capture.CaptureFrame());
	const int w = capture.Width();
	const int h = capture.Height();
	constexpr int fps = 30;
	constexpr int bitrate = 4000000;
	ST::H264Encoder encoder(w, h, fps, bitrate);

	AudioCapture ad_cpt;
	ad_cpt.Start();

	auto last_time = std::chrono::steady_clock::now();
	loop{
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / max_fps_data.load(std::memory_order_acquire))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;
		
		{
			std::lock_guard lock(mtx_close);
			if(client.Closed())
				break;
		}

		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& rgba = capture.GetBuffer();

		if (rgba.empty()) {
			continue;
		}

		auto packets = encoder.EncodeFrame(rgba);
		int32_t pk_size = (int32_t)packets.size();

		client.Send(client.Id());
		client.Send(logger.name);
		{
			std::lock_guard lock(mtx_users);
			client.Send(users[chosen_user]);
		}
		client.Send(ad_cpt.Frames());//move
		client.Send(pk_size);
		for (auto& pk : packets) {
			client.Send(pk);
		}

	}
}

void Show() {
	Window window(800, 600, ((char*)u8"screenTrans: " + logger.name));

#if USE_IMGUI
	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags = 
		//ImGuiConfigFlags_ViewportsEnable |
		ImGuiConfigFlags_DockingEnable |
		ImGuiConfigFlags_NavEnableKeyboard |     // Enable Keyboard Controls
		ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)
	const auto DEFAULT_BK_COLOR = style.Colors[ImGuiCol_WindowBg];

	//io.Fonts->Clear();
	io.Fonts->AddFontFromFileTTF("font/MapleMonoNL-CN-Regular.ttf", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	//io.Fonts->Build();
	//io.Fonts->AddFontFromFileTTF("font/msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window.get().get(), true);
	ImGui_ImplOpenGL3_Init("#version 330");
#endif


	struct swapBuffers{
		Window* window;
		swapBuffers(Window& w): window(&w) {}
		~swapBuffers() {
			window->swapBuffers();
		}
	};

	//always keep this
	auto unused = std::make_unique<TM::Rectangle>(10, 10, 0, 0, window);
	//always keep this

	auto img = std::make_unique<Image>(window, 0, 0, std::vector<uint8_t>{ 0, 0, 0, 0 }, 1, 1, 800, 600);

	double d_time = 0;
	float fps = 0;
	bool show_settings = true;

	auto last_time = std::chrono::steady_clock::now();
	while (!window.closed()) {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / max_fps_video.load(std::memory_order_acquire))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		{
			std::lock_guard lock(mtx_close);
			if (client.Closed())
				break;
		}

#if USE_IMGUI
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
#endif

		window.clear();
		window.renderComps();

		swapBuffers swap_buffers(window); // prevent continue swapbuffers

#if USE_IMGUI
		ImGui::DockingSpace();

		// ============ setting window ============ 
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			show_settings = !show_settings;

		if (show_settings) {
			ImGui::Begin((char*)u8"设置");
			if (d_time > 0.4) {
				fps = ImGui::GetIO().Framerate;
				d_time = 0;
			}
			ImGui::Text("FPS: %.3f", fps);
			constexpr double MAX_MAX_FPS = 100.0;
			constexpr double MIN_MAX_FPS = 5.0;
			auto fd = max_fps_data.load(std::memory_order_acquire);
			auto fv = max_fps_video.load(std::memory_order_acquire);
			ImGui::SliderScalar("max_fps_data",ImGuiDataType_Double, &fd, &MIN_MAX_FPS, &MAX_MAX_FPS);
			ImGui::SliderScalar("max_fps_video", ImGuiDataType_Double, &fv, &MIN_MAX_FPS, &MAX_MAX_FPS);
			max_fps_data.store(fd, std::memory_order_release);
			max_fps_video.store(fv, std::memory_order_release);
			ImGui::Text("target:");
			if (ImGui::BeginCombo("##combo", chosen_user.c_str())) {
				{
					std::lock_guard lock(mtx_users);
					for (const auto& user : users) {
						if (ImGui::Selectable(user.first.c_str())) {
							std::lock_guard lock(mtx_choiceChange);
							chosen_user = user.first;
							println("chosen socket: " << user.second);
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::End();// settings
		}
		// ============ setting window ============ 


		// ============ main window ============ 
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
		ImGui::Begin("main");
		ImGui::End();// main
		style.Colors[ImGuiCol_WindowBg] = DEFAULT_BK_COLOR;
		// ============ main window ============ 

		ImGui::End();//docking end

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));

		d_time += window.deltaTime;

		ST::DecodedFrame frame;
		{
			std::lock_guard<std::mutex> lock(mtx_video_frames);
			if (total_video_frames.empty()) {
				continue;
			}

			frame = std::move(total_video_frames.front());
			total_video_frames.pop();
		}

		int width = frame.width;
		int height = frame.height;
		auto& data = frame.rgba;

		if (data.size() != width * height * 4) {
			continue;
		}

		img->resize(window.width, window.height);
		img->resetData(data, width, height);
	}

#if USE_IMGUI
	// Cleanup
	ImGui::DestroyPlatformWindows();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
#endif
}

void register_handle() {
	Choice choice = -1;
	while (choice == -1) {
		println("choose mode");
		println((int)choice_enter_room << ": enter a room");
		println((int)choice_make_room << ": make a room");
		if (!(std::cin >> choice)) {
			std::cin.clear(); // 清除错误标志
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 丢弃错误行
			choice = -1;
		}
		if (choice == -1 || choice != choice_enter_room && choice != choice_make_room) {
			println("invalid input");
			choice = -1;
		}
	}
	client.Send(choice);

	switch (choice) {
	case choice_make_room: {
		logger.passwd = Room::invalid_passwd;
		while (logger.passwd == Room::invalid_passwd) {
			println("set your password (1~4294967295): ");
			std::cin >> logger.passwd;
		}

		client.Send(logger.passwd);
		auto room_id0 = client.ReceiveParseTo<uint32_t>();
		if (!room_id0) {
			goto err_server_status;
		}
		logger.room_id = *room_id0;
		println("your room id: " << logger.room_id);
	}break;
	case choice_enter_room: {
		uint32_t room_id = Room::invalid_id;
		uint32_t passwd = Room::invalid_passwd;
		for (;;) {
			println("input room_id(1~4294967295), password(1~4294967295):");
			if (!(std::cin >> room_id >> passwd)) {
				std::cin.clear(); // 清除错误标志
				std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // 丢弃错误行
			}
			if (room_id == Room::invalid_id) {
				println("room_id format invalid");
				continue;
			}
			if (passwd == Room::invalid_passwd) {
				println("passwd format invalid");
				continue;
			}

			client.Send(room_id);
			client.Send(passwd);

			auto room_id_ok = client.ReceiveParseTo<bool>();
			if (!room_id_ok.has_value()) {
				goto err_server_status;
			}
			if (!room_id_ok.value()) {
				println("room id not exist");
				continue;
			}

			auto passwd_ok = client.ReceiveParseTo<bool>();
			if (!passwd_ok.has_value()) {
				goto err_server_status;
			}
			if (!passwd_ok.value()) {
				println("password wrong");
				continue;
			}
			break;
		}
		logger.passwd = passwd;
		logger.room_id = room_id;
	}break;
	}

	return;
err_server_status:
	println("server status error");
	system("pause");
	exit(1);
}

#ifndef NDEBUG
#include <filesystem>
#endif//NDEBUG

int main() {
#ifndef NDEBUG
	std::filesystem::path cwd = std::filesystem::current_path();
	std::cout << "当前工作目录：" << cwd << std::endl;
#endif//NDEBUG

	println("your socket: " << client.Id());
	println("input ipv4, port, logging_name:");
	{
		std::string ip;
		uint32_t port;
		std::cin >> ip >> port >> logger.name;
		client.ConnectTo(ip.c_str(), port);
		println("connected to server successfully");
	}
	
	register_handle();
	println("register successfully");

	chosen_user = logger.name;
	users[chosen_user] = client.Id();

	std::jthread send_thread(Send);
	std::jthread recv_thread(Receive);
	Show();

	close_signal = true;

}