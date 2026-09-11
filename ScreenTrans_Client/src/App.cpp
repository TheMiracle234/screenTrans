// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#define _CRT_SECURE_NO_WARNINGS
#include <App.hpp>

#ifndef NDEBUG
#	include <filesystem>
#endif//NDEBUG

#ifdef NDEBUG
extern "C" {
#	include <libavutil/log.h>
}
#endif//NDEBUG

#include <inttypes.h>

#if USE_IMGUI
namespace ImGui {
	void DockingSpace() {
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
#endif

//inline std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }
//inline double duration_to_double(std::chrono::steady_clock::duration d) { return std::chrono::duration<double>(d).count(); }

std::unique_ptr<gl::Window> App::makeWindow() {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto res = std::make_unique<gl::Window>(800, 600, glm::ivec2{ 200, 100 }, "client");
	glfwMakeContextCurrent(res->m_get);
	GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	return res;
}

/*
send: signals, id (, name, audio_frames(, pk_size, pk[n]))
*/
void App::Receive() {
	H264Decoder decoder;

	ad_player.Start();

	auto last_time = std::chrono::steady_clock::now();

	while (!close_signal) {
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
		auto frames = client.ReceiveVec<int16_t>();
		ad_player.PushFrames(*frames);
		{
			std::lock_guard lock(mtx_users);
			users[*other_name] = *id;
		}

		if (*signals & signal_choiceNotMatch) {
			continue;
		}


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
recv: id, name, audio_frames, choose_socket, pk_size, pk[n]
*/
void App::Send() {
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
			if (client.Closed())
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
		client.Send(ad_cpt.Frames());//move
		{
			std::lock_guard lock(mtx_users);
			client.Send(users[chosen_user]);
		}
		client.Send(pk_size);
		for (auto& pk : packets) {
			client.Send(pk);
		}

	}
}

App::Page App::Show() {
	chosen_user = logger.name;
	users[chosen_user] = client.Id();
	std::jthread send_thread{ &App::Send, this };
	std::jthread recv_thread{ &App::Receive, this };


	glfwSetWindowUserPointer(window->m_get, this);
	glfwSetWindowPosCallback(window->m_get, [](GLFWwindow* window, int xpos, int ypos) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		user->window->updatePos({ xpos, ypos });
		});
	glfwSetFramebufferSizeCallback(window->m_get, [](GLFWwindow* window, int width, int height) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		glViewport(0, 0, width, height);
		user->window->updatePos({ width, height });
		user->window->setViewport({ 0,0,width, height });
		});

	constexpr const char* vs = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 texCoords;
void main(){
	texCoords = aTexCoords;
	gl_Position = vec4(aPos.x, -aPos.y, aPos.z, 1.0f);
}
)";
	constexpr const char* fs = R"(
#version 330 core
in vec2 texCoords;
uniform sampler2D sampler;
out vec4 FragColor;
void main(){
	FragColor = texture(sampler, texCoords);
}
)";
	struct imgStruct {
		float pos[3];
		float texCoords[2];
	};

	gl::Shader shader{ vs, fs };
	shader.use();
	shader.setUniform("sampler", 0);
	gl::Texture2D img(1, 1, { 0xFF,0x77,0x44,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
	img.bindTo(0);
	gl::VertexBufferLayout layoutImg;
	layoutImg.push<float>(3);
	layoutImg.push<float>(2);
	gl::VertexArray vaoImg;
	std::vector<imgStruct> verticesImg{
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
		{{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
	};
	gl::VertexBuffer vboImg(layoutImg, gl::toVector<uint8_t>(verticesImg), gl::BufferUsage::Static);
	gl::IndexBuffer eboImg({ 0,1,2,1,2,3 }, gl::BufferUsage::Static);
	vaoImg.bindVboAllAttribs({ 0,1 }, vboImg);


#if USE_IMGUI
	const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif


	struct swapBuffers {
		gl::Window* window;
		swapBuffers(gl::Window* tar) : window{ tar } {}
		~swapBuffers() { window->swapBuffers(); }
	};


	double d_time = 0;
	float fps = 0;
	bool show_settings = true;

	auto last_time = std::chrono::steady_clock::now();
	while (!window->shouldClose()) {
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

		glfwPollEvents();

#if USE_IMGUI
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
#endif

		glClearColor(0.1f, 0.1f, 0.1f, 0.1f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		shader.use();
		vaoImg.bind();
		gl::draw(eboImg, gl::DrawMode::TRIANGLES);

		swapBuffers swap_buffers{window.get()}; // prevent continue swapbuffers

#if USE_IMGUI
		ImGui::DockingSpace();

		// ============ setting window ============ 
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			show_settings = !show_settings;

		if (show_settings) {
			ImGui::Begin((char*)u8"设置");
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			if (d_time > 0.4) {
				fps = ImGui::GetIO().Framerate;
				d_time = 0;
			}
			ImGui::Text("FPS: %.3f", fps);
			constexpr double MAX_MAX_FPS = 100.0;
			constexpr double MIN_MAX_FPS = 5.0;
			auto fd = max_fps_data.load(std::memory_order_acquire);
			auto fv = max_fps_video.load(std::memory_order_acquire);
			ImGui::SliderScalar("max_fps_data", ImGuiDataType_Double, &fd, &MIN_MAX_FPS, &MAX_MAX_FPS);
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
		style->Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
		ImGui::Begin("main");
		ImGui::End();// main
		style->Colors[ImGuiCol_WindowBg] = DEFAULT_BK_COLOR;
		// ============ main window ============ 

		ImGui::End();//docking end

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		d_time += 1 / (double)ImGui::GetIO().Framerate;
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));


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

		img.resize(width, height, 4);
		img.resetData(data, 4);
	}
	println("end ok");
	close_signal.store(true, std::memory_order_release);
	return Page::never;
}

void App::pageRenderBegin(const char* title) {
	if (window->shouldClose()) { exit(1); }
	glfwPollEvents();
#if USE_IMGUI
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
#endif

	glClearColor(0.1f, 0.1f, 0.1f, 0.1f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#if USE_IMGUI
	ImGui::DockingSpace();
	ImGui::Begin(title);
#endif
}

void App::pageRenderEnd() {
#if USE_IMGUI
	ImGui::End();// settings
	ImGui::End();//docking end
	// Rendering
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif	
	window->swapBuffers();
}

App::Page App::chooseMode() {
	Choice choice{ 0 };
	while(1) {
		PageRenderGuard pageRenderGuard{ this, "choose mode" };
		if (ImGui::Button("<-")) { client.Send(true); return Page::connectToServer; }
		ImGui::RadioButton("enter a room", &choice, choice_enter_room);
		ImGui::RadioButton("make a room", &choice, choice_make_room);
		if (choice == choice_invalid) { continue; }
		if (ImGui::Button("->")) { 
			client.Send(false);
			client.Send(choice);
			switch (choice) {
			case choice_enter_room: return Page::enterRoom;break;
			case choice_make_room: return Page::makeRoom;break;
			}
		}
	}
	return Page::never;
}

App::Page App::makeRoom() {
	logger.room_id = Room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	while (1) {
		while (1) {
			PageRenderGuard PageRenderGuard{ this, "passwd" };
			if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
			ImGui::InputScalar("passwd", ImGuiDataType_U32, &logger.passwd);
			if (logger.passwd == Room::invalid_passwd) {
				ImGui::Text("invalid passwd: %d", (int)Room::invalid_passwd);
			}
			else if(ImGui::Button("->")) { break; }
		}
		client.Send(false);
		client.Send(logger.passwd);
		auto room_id0 = client.ReceiveParseTo<uint32_t>();
		if (!room_id0) {
			return Page::serverStatusError;
		}
		logger.room_id = *room_id0;
		return Page::Show;
	}
}

App::Page App::enterRoom() {
	logger.room_id = Room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	uint32_t room_id{ Room::invalid_id };
	uint32_t passwd{ Room::invalid_passwd };
	bool room_id_not_exist{ false };
	bool passwd_wrong{ false };
	for (;;) {
		PageRenderGuard pageRenderGuard(this, "enter room");
		if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
		ImGui::InputScalar("room_id", ImGuiDataType_U32, &room_id);
		ImGui::InputScalar("passwd", ImGuiDataType_U32, &passwd);
		ImGui::Text("both: 1~4294967295");
		bool want_continue{ false };
		if (room_id == Room::invalid_id)	{ ImGui::Text("room_id format invalid"); want_continue = true; } 
		if (passwd == Room::invalid_passwd) { ImGui::Text("passwd format invalid"); want_continue = true; } 
		if (want_continue) { continue; }
		if (room_id_not_exist)	{ ImGui::Text("room id not exist"); } 
		if (passwd_wrong)		{ ImGui::Text("passwd wrong"); }
		if (ImGui::Button("->")) {
			client.Send(false);
			client.Send(room_id);
			client.Send(passwd);
			auto room_id_ok = client.ReceiveParseTo<bool>();
			if (!room_id_ok.has_value()){ return Page::serverStatusError; }
			if (!room_id_ok.value())	{ room_id_not_exist = true; continue; }
			else { room_id_not_exist = false; }
			auto passwd_ok = client.ReceiveParseTo<bool>();
			if (!passwd_ok.has_value())	{ return Page::serverStatusError; }
			if (!passwd_ok.value())		{ passwd_wrong = true; continue; }
			else { passwd_wrong = false; }
			break;
		}
	}
	logger.passwd = passwd;
	logger.room_id = room_id;
	return Page::Show;
}

App::Page App::serverStatusError() {
	while (1) {
		PageRenderGuard pageRenderGuard{ this, "server status error" };
		ImGui::Text("server status error. please close the window");
	}
}

App::Page App::connectToServer() {
	client = Client{ TM::Socket::TCP, Socket::IPV4 };
	logger.name = {};
	constexpr size_t buf_size = 256;
	char ipv4[buf_size]{};
	constexpr uint32_t invalid_port{ 0 };
	uint32_t port{ invalid_port };
	char name[buf_size]{};

	bool first{ true };
	loop {
		loop {
			PageRenderGuard pageRenderGuard{this, "connect to server" };
			ImGui::Text("your socket: %zu", static_cast<size_t>(client.Id()));
			ImGui::InputText("target_ipv4", ipv4, buf_size);
			ImGui::InputScalar("target_port", ImGuiDataType_U32, &port);
			ImGui::InputText("your_name", name, buf_size, ImGuiInputTextFlags_CharsNoBlank);
			bool ok{ true };
			if (!first)					{ ImGui::Text("connect failed"); ok = false; } 
			if (strlen(name) == 0)		{ ImGui::Text("name should not be blank"); ok = false; }
			if (port == invalid_port)	{ ImGui::Text("%s: port invalid - should be a unsigned number not 0", port); ok = false; }
			if (ok && ImGui::Button("connect")) { break; }
		}
		if (client.ConnectTo(ipv4, port)) {
			println("connected to server successfully");
			break;
		}
		else {
			println("connected to server failed");
			first = false;
		}
	}
	logger.name = name;
	return Page::chooseMode;
}

void App::run() {
#ifdef NDEBUG
	av_log_set_level(AV_LOG_ERROR); // ffmpeg log
#endif//NDEBUG

	while (!close_signal.load(std::memory_order_acquire)) {
		switch (page) {
		case Page::connectToServer: page = connectToServer(); break;
		case Page::chooseMode: page = chooseMode(); break;
		case Page::makeRoom: page = makeRoom(); break;
		case Page::enterRoom: page = enterRoom(); break;
		case Page::Show: page = Show(); break;
		case Page::serverStatusError: page = serverStatusError(); break;
		}
	}

}

App::App() {
#if USE_IMGUI
	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	io = &ImGui::GetIO();
	io->ConfigFlags =
		//ImGuiConfigFlags_ViewportsEnable |
		ImGuiConfigFlags_DockingEnable |
		ImGuiConfigFlags_NavEnableKeyboard |     // Enable Keyboard Controls
		ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	ImGui::StyleColorsDark();

	// Setup scaling
	style = &ImGui::GetStyle();
	style->ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style->FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

	//io.Fonts->Clear();
	io->Fonts->AddFontFromFileTTF("font/MapleMonoNL-CN-Regular.ttf", 15.0f, nullptr, io->Fonts->GetGlyphRangesChineseFull());
	//io.Fonts->Build();
	//io.Fonts->AddFontFromFileTTF("font/msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
	ImGui_ImplOpenGL3_Init("#version 330");
#endif
}

App::~App() {
#if USE_IMGUI
	// Cleanup
	ImGui::DestroyPlatformWindows();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
#endif
}

int main() {
	App app{};
	app.run();
}