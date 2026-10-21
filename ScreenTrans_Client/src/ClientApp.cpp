// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#define _CRT_SECURE_NO_WARNINGS
#include <ClientApp.hpp>

#include <util/Timer.hpp>

#ifndef NDEBUG
#	include <filesystem>
#endif//NDEBUG

#ifdef NDEBUG
	extern "C" {
#	include <libavutil/log.h>
	}
#endif//NDEBUG

#include <inttypes.h>
#include <cstdlib>

#include <atomic_imgui.hpp>

#ifdef _WIN32
#	include <timeapi.h>
#	pragma comment(lib, "winmm.lib")
#endif


#if defined(_WIN32)
#   define FONT_CJK_PATH "C:/Windows/Fonts/msyh.ttc"          // 微软雅黑
#elif defined(__APPLE__)
#   define FONT_CJK_PATH "/System/Library/Fonts/PingFang.ttc" // 苹方
#elif defined(__linux__)
#   define FONT_CJK_PATH "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
#else
	static_assert(false, "unsupported platform font");
#endif

template <typename CB>
[[nodiscard]] std::size_t cbPop(CB& cb, typename CB::pointer out, std::size_t count) {
    using T = typename CB::value_type;
    if (count == 0 || cb.empty()) return 0;
    const std::size_t got = std::min<std::size_t>(count, cb.size());

    if constexpr (std::is_trivially_copyable_v<T>) {
        // 按字节复制，最快
        auto a1 = cb.array_one();
        auto a2 = cb.array_two();
        const std::size_t n1 = std::min(got, a1.second);
        std::memcpy(out, a1.first, n1 * sizeof(T));
        if (n1 < got) {
            std::memcpy(out + n1, a2.first, (got - n1) * sizeof(T));
        }
    }
    else if constexpr (std::is_copy_assignable_v<T>) {
        // 非平凡但可赋值：std::copy_n 会调 operator=
        std::copy_n(cb.begin(), got, out);
    }
    else {
		assert(false);
	}

    cb.erase_begin(got);
    return got;
}

#if USE_IMGUI
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

	void PushU64(uint64_t id) {
		ImGui::PushID(static_cast<int>( id & 0xFFFFFFFFu));
		ImGui::PushID(static_cast<int>((id >> 32) & 0xFFFFFFFFu));
	}

	void PopU64() {
		ImGui::PopID();
		ImGui::PopID();
	}

	struct PushU64Guard {
		PushU64Guard(uint64_t id) { PushU64(id); }
		~PushU64Guard() { PopU64(); }
	};

	struct PushIdGuard {
		PushIdGuard(int id) { ImGui::PushID(id); }
		~PushIdGuard() { ImGui::PopID(); }
	};
}
#endif

std::unique_ptr<gl::Window> ClientApp::makeWindow() {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto res = std::make_unique<gl::Window>(1200, 600, glm::ivec2{ 200, 100 }, "FlexMeet-client");
	glfwMakeContextCurrent(res->m_get);
	GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	return res;
}

void ClientApp::storeUiState() {
	std::FILE* const file = std::fopen("FlexMeet.ini", "wb");
	DEFER([file]() { std::fclose(file); });
	assert(file);
	Ui buf_ui;

	constexpr size_t BEGIN = __LINE__ + 1; // === BEGIN - 1 ===
	memcpy(	buf_ui.p1.ipv4,							p1.ipv4, sizeof(p1.ipv4));//
			buf_ui.p1.port							= net::host_to_network(p1.port);
	memcpy(	buf_ui.p1.name,							p1.name, sizeof(p1.ipv4));
			buf_ui.p2.passwd						= net::host_to_network(p2.passwd);//
			buf_ui.p3.room_id						= net::host_to_network(p3.room_id);//
			buf_ui.p3.passwd						= net::host_to_network(p3.passwd);//
			buf_ui.send.fps							= net::host_to_network(get(ui.send.fps));//
			buf_ui.send.share_audio					= net::host_to_network(get(ui.send.share_audio));
			buf_ui.send.share_video					= net::host_to_network(get(ui.send.share_video));
			buf_ui.video.fps						= net::host_to_network(get(ui.video.fps));//
			buf_ui.video.encode.fps					= net::host_to_network(get(ui.video.encode.fps));
			buf_ui.video.encode.bitrate				= net::host_to_network(get(ui.video.encode.bitrate));
			buf_ui.video.encode.width				= net::host_to_network(get(ui.video.encode.width));
			buf_ui.video.encode.height				= net::host_to_network(get(ui.video.encode.height));
			buf_ui.room_user.host.set_room_passwd	= net::host_to_network(get(ui.room_user.host.set_room_passwd));
			buf_ui.room_user.host.mute_all_keep		= net::host_to_network(get(ui.room_user.host.mute_all_keep));
	constexpr size_t END = __LINE__; // === END ===
	static_assert(END - BEGIN == count_basic_members<Ui>());

	std::fwrite(&buf_ui, sizeof(buf_ui), 1, file);
}

void ClientApp::EnableCamera(bool enable) {
	if (enable) {
		if(!m_cameraCapture.IsOpened()) {
			bool ret = m_cameraCapture.Open(0, 1280, 720);
			if (ret) {
				m_enableCamera = true;
				std::cout << "摄像头开启成功" << std::endl;
			}
			else {
				m_enableCamera = false;
				std::cout << "摄像头开启失败" << std::endl;
			}
			ST::DecodedFrame dummy;
			while (total_capture_frames.pop(dummy)) {}
			while (send_camera_packets.pop(dummy.rgba)) {}
			while (total_camera_frames.pop(dummy)) {}
		}
	}
	else {
		if(m_cameraCapture.IsOpened()) {
			m_cameraCapture.Close();
		}
		m_enableCamera = false;
	}
}

void ClientApp::CameraCaptureThread() {
	DEFER{
		[this]() {p_show.wait_threads.release(); }
	};

	std::vector<uint8_t> camBuf;
	int camW = 0, camH = 0;

	auto last_time = std::chrono::steady_clock::now();
	const double fps = 30.0;

	while (!p_show.ended() && !clientClosed()) {
		auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / fps)
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		if (!get(ui.send.share_camera)) continue;

		bool wantCamera = get(ui.send.share_camera);
		
		if (wantCamera && !m_cameraCapture.IsOpened()) {
			EnableCamera(true);
		}
		
		if (!wantCamera && m_cameraCapture.IsOpened()) {
			EnableCamera(false);
		}

		if (!wantCamera) {
			continue;
		}

		bool camHasFrame = m_cameraCapture.GetFrame(camBuf, camW, camH);

		if (camHasFrame)
		{
			ST::DecodedFrame frame;
			frame.width = camW;
			frame.height = camH;
			frame.rgba.swap(camBuf);
			total_capture_frames.push(std::move(frame));
		}
	}

}
//发送转码
void ClientApp::CameraCaptureEncode() {
	DEFER{
		[this]() {p_show.wait_threads.release(); }
	};
	ST::DecodedFrame frame;
	while (!p_show.ended() && !clientClosed()) {
		if (total_capture_frames.pop(frame)) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (p_show.ended() || clientClosed()) return;

	int w = frame.width & ~1;
	int h = frame.height & ~1;

	ST::H264Encoder encoder(w, h, get(ui.video.encode.fps), get(ui.video.encode.bitrate));
	auto last_time = std::chrono::steady_clock::now();

	while (!p_show.ended() && !clientClosed()){
		if (!get(ui.send.share_camera)){
			std::this_thread::sleep_for(20ms);
			continue;
		}

		if (!total_capture_frames.pop(frame)){
			continue;
		}

		auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.send.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		auto pkts = encoder.EncodeFrame(frame.rgba);
        

        if (!pkts.empty()) {
            //printf("[Encode] done, packets=%zu\n", pkts.size());
            
            for (auto& pkt : pkts) {
				if (!send_camera_packets.push(std::move(pkt))) {
					break;
				}
            }
        }
	}
}

void ClientApp::CameraDecode() {
	DEFER{ [this]() {
		p_show.wait_threads.release();
	} };
	H264Decoder decoder;
	std::vector<std::vector<uint8_t>> pkts(recv_camera_packets_count);
	while (!p_show.ended() && !clientClosed()) {
		const size_t pkts_count = recv_camera_packets.pop(pkts.data(), recv_camera_packets_count);
		if (pkts_count == 0) {
			std::this_thread::sleep_for(std::chrono::microseconds(500));
			continue;
		}
		for (size_t i = 0; i < pkts_count; ++i) {
			auto frames = decoder.DecodePacket(pkts[i]);
			for (auto& frame : frames) {
				total_camera_frames.push(std::move(frame));
			}
		}
	}
}


void ClientApp::loadUiState() {
	std::FILE* const file = std::fopen("FlexMeet.ini", "rb");
	DEFER([file]() { if (file) { std::fclose(file); }});
	if (!file) { return; }
	std::fseek(file, 0, SEEK_END);
	const auto file_size = std::ftell(file);
	if (file_size != sizeof(Ui)) { return; }
	std::rewind(file);
	Ui buf_ui;
	fread(&buf_ui, sizeof(Ui), 1, file);
	constexpr size_t BEGIN = __LINE__ + 1; // === BEGIN -1 ===
	memcpy( p1.ipv4,							buf_ui.p1.ipv4, sizeof(p1.ipv4));//
			p1.port	=							net::network_to_host(buf_ui.p1.port);
	memcpy( p1.name,							buf_ui.p1.name, sizeof(p1.ipv4));
			p2.passwd =							net::network_to_host(buf_ui.p2.passwd);//
			p3.room_id =						net::network_to_host(buf_ui.p3.room_id);//
			p3.passwd =							net::network_to_host(buf_ui.p3.passwd);//
	set(	ui.send.fps,						net::network_to_host(buf_ui.send.fps));//
	set(	ui.send.share_audio,				net::network_to_host(buf_ui.send.share_audio));
	set(	ui.send.share_video,				net::network_to_host(buf_ui.send.share_video));
	set(	ui.video.fps,						net::network_to_host(buf_ui.video.fps));//
	set(	ui.video.encode.fps,				net::network_to_host(buf_ui.video.encode.fps));
	set(	ui.video.encode.bitrate,			net::network_to_host(buf_ui.video.encode.bitrate));
	set(	ui.video.encode.width,				net::network_to_host(buf_ui.video.encode.width));
	set(	ui.video.encode.height,				net::network_to_host(buf_ui.video.encode.height));
	set(	ui.room_user.host.set_room_passwd,	net::network_to_host(buf_ui.room_user.host.set_room_passwd));
	set(	ui.room_user.host.mute_all_keep,	net::network_to_host(buf_ui.room_user.host.mute_all_keep));
	constexpr size_t END = __LINE__; // === END ===
	static_assert(END - BEGIN == count_basic_members<Ui>());
}

template<bool lock_users>
bool ClientApp::isHost() {
	if constexpr (lock_users) {
		std::lock_guard lock(mtx_users);
		return get(users[get(logger.id)].is_host);
	}
	else {
		return get(users[get(logger.id)].is_host);
	}
}

bool ClientApp::clientClosed() {
	std::lock_guard lock(mtx_close);
	return client.Closed();
}

void ClientApp::Receive() {
	DEFER{ [this]() {
		p_show.wait_threads.release();
	}};

	struct {
		Signal signals{};
		user::id_t id{ user::invalid_id };
		std::string other_name{};
		bool is_host_from_server{};
		uint32_t room_passwd{};
		bool is_host_from_client{};
		user::id_t wanna_kick_off{};
		//bool mute_all_once{};
		//bool mute_all_keep{};
		//bool share_audio{};
		//std::vector<float> audioFrames{};
		bool has_words_msg{};
		user::id_t words_msg_target{};
		char words_msg[words_msg_bytes];
		bool share_video{};
		int32_t pk_size{};
		std::vector<uint8_t> pk{};

		bool share_camera{};
		int32_t cam_pk_size{};
		std::vector<uint8_t> cam_pk{};

		void restore() {
		}
	} rv;

	while (!p_show.ended() && !clientClosed()) {
		rv.restore();
		bool recv_ok{true};
		if (!client.ReceiveBy(rv.signals) ||  
			!client.ReceiveBy(rv.id)) [[unlikely]] 
		{
			break;
		}
		if (rv.signals & signal_closed) [[unlikely]] {
			{
				std::lock_guard lock(mtx_users);
				auto count = std::erase_if(users, [&](auto& user) { return user.first == rv.id;});
			}
			set(ui.room_user.chosen_user, get(logger.id));
			continue;
		}

		client.ReceiveBy(rv.other_name);
		if (!client.ReceiveBy(rv.is_host_from_server)) [[unlikely]] { break; }
		if (rv.is_host_from_server && get(logger.id) == rv.id) {
			if (!client.ReceiveBy(rv.room_passwd)) [[unlikely]] { break; }
			set(ui.room_user.host.room_passwd, rv.room_passwd);
		}
		if (!client.ReceiveBy(rv.is_host_from_client)) [[unlikely]] { break; }
		if (rv.is_host_from_client) {
			client.ReceiveBy(rv.wanna_kick_off);
			//client.ReceiveBy(rv.mute_all_once);
			//client.ReceiveBy(rv.mute_all_keep);
		}
		//if (!client.ReceiveBy(rv.share_audio)) [[unlikely]] { break; }
		//if (rv.share_audio) {
			//client.ReceiveBy(rv.audioFrames);
		//}
		//else {
			//rv.audioFrames.resize(0);
		//}

		if (!client.ReceiveBy(rv.has_words_msg)) [[unlikely]] { break; }
		if (rv.has_words_msg) [[unlikely]] {
			client.ReceiveBy(rv.words_msg_target);
			if (rv.words_msg_target == user::public_target) {
				client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
				{
					std::lock_guard lock0(mtx_users);
					std::lock_guard lock1(ui.send.total_words_msg.mtx);
					ui.send.total_words_msg.buf.push_back(users[rv.id].name + ": " + rv.words_msg);
				}
			}
			else if (rv.words_msg_target == logger.id) {
				client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
				{
					std::lock_guard lock(mtx_users);
					users[rv.id].total_words_msg.buf.push_back(users[rv.id].name + ": " + rv.words_msg);
				}
			}
		}

		if (!client.ReceiveBy(rv.share_video)) [[unlikely]] { break; }

		if (rv.is_host_from_client) {
			if (rv.wanna_kick_off == get(logger.id)) [[unlikely]] {
				p_show.setEnd(true, ShowPara::End::kickOff);
			}
			//if (rv.mute_all_once && rv.id != logger.id) [[unlikely]] {
			//	set(ui.send.share_audio, false);
			//}
			//if (rv.mute_all_keep && rv.id != logger.id) {
			//	set(ui.send.share_audio, false);
			//	set(ui.room_user.is_muted_keep, true);
			//}
			//else {
			//	set(ui.room_user.is_muted_keep, false);
			//}
		}
		{
			std::lock_guard lock(mtx_users);
			auto [itr, first] = users.try_emplace(rv.id);
			itr->second.name = rv.other_name;
			auto& user = itr->second;
			//user.audioBuf.push(rv.audioFrames.begin(), rv.audioFrames.end());
			//set(user.share_audio, rv.share_audio);
			set(user.is_host, rv.is_host_from_server);
			set(user.share_video, rv.share_video);
		}

		if (rv.signals & signal_choiceNotMatch) { continue; }

		// each packet
		if (rv.share_video) {
			if (!client.ReceiveBy(rv.pk_size)) [[unlikely]] { break; }
			const net::EndSrcTar endSrcTar{
				rv.signals & signal_littleEnd ? net::End::little : net::End::big, 
				net::End::native
			};
			for (int i = 0;i < rv.pk_size;++i) {
				if (!client.ReceiveBy(rv.pk, endSrcTar)) [[unlikely]] { break; }
				recv_screen_packets.push(std::move(rv.pk));
			}
		}

		if (!client.ReceiveBy(rv.share_camera)) [[unlikely]] { break; }
		{
			std::lock_guard lock(mtx_users);
			users[rv.id].share_camera = rv.share_camera;
		}
		if (rv.share_camera)
		{
			if (!client.ReceiveBy(rv.cam_pk_size)) [[unlikely]] { break; }
			const net::EndSrcTar endSrcTar{
				rv.signals & signal_littleEnd ? net::End::little : net::End::big,
				net::End::native
			};
			for (int i = 0; i < rv.cam_pk_size; i++)
			{
				if (!client.ReceiveBy(rv.cam_pk, endSrcTar)) [[unlikely]] { break; }
				recv_camera_packets.push(std::move(rv.cam_pk));
			}
		}
	}
}

void ClientApp::Send() {
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};

	auto last_time = std::chrono::steady_clock::now();
	util::Timer timer;
	timer.restart();
	double sum_time{};
	while(!p_show.ended() && !clientClosed()){
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.send.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		{
			const double sec = timer.seconds();
			sum_time += sec;
			if (sum_time > 0.4) {
				set(ui.send.real_fps, 1 / sec);
				sum_time = 0;
			}
		}

		timer.restart();

		bool share_video = get(ui.send.share_video);

		std::vector<std::vector<uint8_t>> packets(send_screen_packets_count);
		int32_t pk_size = static_cast<int32_t>(send_screen_packets.pop(packets.data(), send_screen_packets_count));

		bool is_host_from_client = isHost<true>();
		assert(get(logger.id) != user::invalid_id);
		client.Send(net::End::native == net::End::little);
		client.Send(get(logger.id));
		{
			std::lock_guard lock(logger.mtx_name);
			client.Send(logger.name);
		}
		client.Send(is_host_from_client);
		if (is_host_from_client) {
			client.Send(get(ui.room_user.host.set_room_passwd));
			set(ui.room_user.host.set_room_passwd, Room::invalid_passwd);
			client.Send(get(ui.room_user.host.wanna_kick_off));
			set(ui.room_user.host.wanna_kick_off, user::invalid_id);
			client.Send(get(ui.room_user.host.transfer_host));
			set(ui.room_user.host.transfer_host, user::invalid_id);
			//client.Send(get(ui.room_user.host.mute_all_once));
			//set(ui.room_user.host.mute_all_once, false);
			//client.Send(get(ui.room_user.host.mute_all_keep));
		}
		bool has_words_msg;
		{
			std::lock_guard lock(ui.send.words_msg.mtx);
			has_words_msg = ui.send.words_msg.buf[0] != '\0';
		}
		client.Send(has_words_msg);
		if (has_words_msg) [[unlikely]] {
			user::id_t target = get(ui.send.words_msg_target);
			client.Send(target);
			char buf[sizeof(ui.send.words_msg.buf)];
			{
				std::lock_guard lock(ui.send.words_msg.mtx);
				memcpy(buf, ui.send.words_msg.buf, sizeof(buf));
				ui.send.words_msg.buf[0] = '\0';
			}
			client.Send(buf, sizeof(buf));
			if (target != user::public_target) {
				std::lock_guard lock(mtx_users);
				auto& user = users[target];
				user.total_words_msg.buf.push_back(logger.name + ": " + buf);
			}
		}
		client.Send(get(ui.room_user.chosen_user));
		client.Send(share_video);
		if (share_video) {
			client.Send(pk_size);
			for (size_t i = 0;i < pk_size;++i) {
				auto& packet = packets[i];
				//client.Send(packet.data(), packet.size());
				client.Send(packet);
			}
		}

		bool share_camera = get(ui.send.share_camera);
		std::vector<std::vector<uint8_t>> cam_packets(send_camera_packets_count);
		int32_t cam_pk_size = static_cast<int32_t>(send_camera_packets.pop(cam_packets.data(), send_camera_packets_count));

		client.Send(share_camera);
		if (share_camera){
			client.Send(cam_pk_size);
			for (int32_t i = 0; i < cam_pk_size; i++){
				client.Send(cam_packets[i]);
			}
		}
	}
}

void ClientApp::audioSend(){
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	ad_cpt.Start();
	std::vector<float> buf_audio{};

	auto last_time = std::chrono::steady_clock::now();
	while (!p_show.ended() && !clientClosed()) {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / (audio::sampleRate / audio::periodSizeInFrames))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		client_audio.Send(get(logger.id));
		faudio_t faudio{ 0 };
		if (net::End::native == net::End::little) { faudio |= faudio_little_end; }
		if (get(ui.send.share_audio)) { faudio |= faudio_share; }
		if (isHost<true>()) { faudio |= faudio_is_host; }
		if (get(ui.room_user.host.mute_all_once)) [[unlikely]] { faudio |= faudio_mute_all_once; }
		if (get(ui.room_user.host.mute_all_keep)) { faudio |= faudio_mute_all_keep; }
		set(ui.room_user.host.mute_all_once, false);

		client_audio.Send(faudio);
		if (faudio & faudio_share) {
			if (ui.room_user.is_muted_keep) {
				float tmp;
				client_audio.Send(&tmp, 0);
			}
			else {
				ad_cpt.giveFrames(buf_audio);
				client_audio.Send(buf_audio.data(), sizeof(buf_audio[0]) * buf_audio.size());
			}
			//client_audio.Send(ui.room_user.is_muted_keep ? std::vector<float>{} : std::move(ad_cpt.giveFrames(buf_audio)));//move
		}
	}
}

void ClientApp::audioRecv(){
	DEFER{ [this]() {
		audioPlayer.stop();
		p_show.wait_threads.release();
	}};
	audioPlayer.start();


	struct {
		user::id_t id{};
		faudio_t faudio{};
		std::vector<float> audioFrames{};
		void restore() {
		
		}
	} rv{};

	while (!p_show.ended() && !clientClosed()) {
		client_audio.ReceiveBy(rv.id);
		client_audio.ReceiveBy(rv.faudio);
		
		const net::EndSrcTar endSrcTar{
			rv.faudio & faudio_little_end ? net::End::little : net::End::big,
			net::End::native
		};

		if (rv.faudio & faudio_share) {
			client_audio.ReceiveBy(rv.audioFrames, endSrcTar);
		}
		else {
			rv.audioFrames.resize(0);
		}

		if (rv.faudio & faudio_is_host) {
			if ((rv.faudio & faudio_mute_all_once) && rv.id != logger.id) [[unlikely]] {
				set(ui.send.share_audio, false);
			}
			if ((rv.faudio & faudio_mute_all_keep) && rv.id != logger.id) {
				set(ui.send.share_audio, false);
				set(ui.room_user.is_muted_keep, true);
			}
			else {
				set(ui.room_user.is_muted_keep, false);
			}
		}
		{
			std::lock_guard lock(mtx_users);
			auto [itr, first] = users.try_emplace(rv.id);
			auto& user = itr->second;
			user.audioBuf.insert(user.audioBuf.end(), rv.audioFrames.begin(), rv.audioFrames.end());
			//user.audioBuf.push(rv.audioFrames.begin(), rv.audioFrames.end());
			set(user.share_audio, rv.faudio & faudio_share);
		}
	}
}

void ClientApp::ScreenCaptureEncode() {
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	ScreenCapture capture;
	if (!capture.Initialize()) {
		std::cerr << "capture.Initialize() error\n";
		exit(1);
	}

	while (!capture.CaptureFrame());
	const int w = capture.Width() & ~1;
	const int h = capture.Height() & ~1;
	set(ui.video.encode.width, w);
	set(ui.video.encode.height, h);
	ui.video.encode.wait_wh.release();
	ST::H264Encoder encoder(w, h, ui.video.encode.fps, ui.video.encode.bitrate);

	auto last_time = std::chrono::steady_clock::now();
	while (!p_show.ended() && !clientClosed()) {
		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& rgba = capture.GetBuffer();
		if (rgba.empty()) {
			continue;
		}

		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.send.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;


		bool share_audio = get(ui.send.share_audio);
		bool share_video = get(ui.send.share_video);

		encoder.Reset(w, h, ui.video.encode.width, ui.video.encode.height, ui.video.encode.fps, ui.video.encode.bitrate);
		set(ui.video.encode.width, encoder.Width());
		set(ui.video.encode.height, encoder.Height());

		auto pkts = encoder.EncodeFrame(rgba);
		for(auto& pkt: pkts){
			send_screen_packets.push(std::move(pkt));
		}
	}
}

void ClientApp::ScreenDecode() {
	DEFER{ [this]() {
		p_show.wait_threads.release();
	} };
	H264Decoder decoder;
	std::vector<std::vector<uint8_t>> pkts(recv_screen_packets_count);
	while (!p_show.ended() && !clientClosed()) {
		const size_t pkts_count = recv_screen_packets.pop(pkts.data(), recv_screen_packets_count);
		for (size_t i = 0;i < pkts_count;++i) {
			auto frames = decoder.DecodePacket(pkts[i]);
			for (auto& frame : frames) {
				total_video_frames_v1.push(std::move(frame));
			}
		}
	}
}

void ClientApp::CameraCapture() {

}

void ClientApp::audioMix()
{
	DEFER{
		[this]() { p_show.wait_threads.release(); }
	};

	//constexpr int sleep_ms = 10;
	//constexpr int target_fps = 1000 / sleep_ms;
	//const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
	//	std::chrono::duration<boost::float64_t>(1.0 / target_fps)
	//);

	//constexpr size_t samples = sleep_ms * audio::sampleRate / 1000 * audio::channels;

	std::vector<float> mixBuf{};
	std::vector<float> getBuf{};

	util::Timer timer;

	timer.restart();
	while (!p_show.ended() && !clientClosed()) {
		std::this_thread::sleep_for(50ms);

		const int sleep_ms = static_cast<int>(timer.milliseconds());
		timer.restart();
		const size_t samples = sleep_ms * audio::sampleRate / 1000 * audio::channels;
		mixBuf.resize(samples, 0);
		getBuf.resize(samples, 0);

		std::fill( mixBuf.begin(), mixBuf.end(), 0.0f );

		size_t realSamples = 0;

		{
			std::lock_guard lock{ mtx_users };
			// 每轮 pop 前先修剪
			const size_t target_watermark = audio::sampleRate * audio::channels / 5;   // 200ms
			for (auto& user : users) {
				const size_t thisSamples = cbPop(user.second.audioBuf, getBuf.data(), samples);
				//const size_t thisSamples = user.second.audioBuf.pop( getBuf.data(), samples );
				realSamples = std::max( realSamples, thisSamples );
				bool audio_playing{ false };
				if (user.first != logger.id) {
					for (size_t i = 0; i < thisSamples; ++i) {
						audio_playing |= std::abs(getBuf[i]) > User::audio_playing_least;
						mixBuf[i] += getBuf[i];
					}
				}
				// no self audio
				else {
					for (size_t i = 0; i < thisSamples; ++i) {
						audio_playing |= std::abs(getBuf[i]) > User::audio_playing_least;
					}
				}
				set(user.second.audio_playing, audio_playing);
			}
		}

		if (realSamples == 0) { continue; }

		auto max_it =
			std::ranges::max_element(
				mixBuf.begin(),
				mixBuf.begin() + realSamples,
				[](float v1, float v2) {
					return std::abs(v1) <
						std::abs(v2);
				}
			);

		if ( max_it != mixBuf.begin() + realSamples) {
			const float maxVal =
				std::abs(*max_it);

			if (maxVal > 1.0f) {

				std::ranges::for_each(
					mixBuf.begin(),
					mixBuf.begin() + realSamples,
					[maxVal](float& v) {
						v /= maxVal;
					}
				);
			}
		}
		{
#if USE_ASR
			std::lock_guard lock(mtx_audioFramesLong);
			audioFramesLong.insert(audioFramesLong.end(), mixBuf.begin(), mixBuf.begin() + realSamples);
#endif
		}
		audioUser.pushFrames(
			mixBuf.data(),
			realSamples
		);
	}
}

void ClientApp::asrRecv()
{
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	std::vector<float> audioBuf;
	while(!p_show.ended() && !clientClosed()) {
		std::this_thread::sleep_for(200ms);

		{
			std::lock_guard lock(mtx_audioFramesLong);
			audioBuf.resize(audioFramesLong.size());
			std::copy(audioFramesLong.begin(), audioFramesLong.end(), audioBuf.begin());
		}
		if (audioBuf.empty()) { continue; }


		int out_bytes = 0;
		 println( "asr.transcribe begin, " << "samples = " << audioBuf.size() << ", seconds = " << static_cast<boost::float64_t>( audioBuf.size() ) / 16000.0 );

#if USE_ASR
		 const char* str = plug.asr.transcribe( audioBuf.data(), static_cast<int>( audioBuf.size() ), &out_bytes );
#else
		 const char* str = nullptr;
#endif
		/*
		 * ========================================================
		 * 保存识别结果
		 * ========================================================
		 */
		if ( str ) {
			printf("got it\n");
			{
				std::lock_guard lock(mtx_audioFramesLong);
				audioFramesLong.clear();
			}
			{
				std::lock_guard lock(mtx_speechWords);
				speechWords.assign(str, out_bytes);
				printf("get: %s\n", speechWords.c_str());
			}
		} else if(speechWords.size() > audio::sampleRate * audio::channels * 20){
			printf("not yet\n");
			speechWords.clear();
		}
	}
}

void ClientApp::ShowPara::setEnd(bool ended, End showEnd) {
	set(m_ended, ended);
	m_showEnd = showEnd;
}

void ClientApp::ShowPara::restore() {
	set(m_ended, false);
	m_showEnd = End::Default;
}

void ClientApp::restoreAfterShow() {
	p_show.restore();
	users.clear();
	glfwSetWindowUserPointer(window->m_get, this);
	glfwSetWindowPosCallback(window->m_get, [](GLFWwindow* window, int xpos, int ypos) {
		auto user = static_cast<ClientApp*>(glfwGetWindowUserPointer(window));
		user->window->updatePos({ xpos, ypos });
		});
	glfwSetFramebufferSizeCallback(window->m_get, [](GLFWwindow* window, int width, int height) {
		auto user = static_cast<ClientApp*>(glfwGetWindowUserPointer(window));
		glViewport(0, 0, width, height);
		user->window->setViewport({ 0,0,width, height });
		});
}

ClientApp::Page ClientApp::Show() {

	set(ui.room_user.chosen_user, get(logger.id));
	{
		std::lock_guard lock2{ mtx_users };
		auto [itr, first] = users.try_emplace(get(ui.room_user.chosen_user));
		itr->second.name = logger.name;
	}

	constexpr size_t thread_count = 11;

	DEFER{ [this, thread_count]() {
		client.Close();
		client_audio.Close();
		for (size_t i = 0;i < thread_count;++i) {
			p_show.wait_threads.acquire();
		}
		restoreAfterShow();
	}};

	std::jthread threads[] = {
		std::jthread{ &ClientApp::audioMix, this },
		std::jthread{ &ClientApp::Send, this },
		std::jthread{ &ClientApp::Receive, this },
		std::jthread{ &ClientApp::audioSend, this },
		std::jthread{ &ClientApp::audioRecv, this },
		std::jthread{ &ClientApp::asrRecv, this },
		std::jthread{ &ClientApp::ScreenCaptureEncode, this },
		std::jthread{ &ClientApp::ScreenDecode, this },
		std::jthread{ &ClientApp::CameraCaptureThread, this },
		std::jthread{ &ClientApp::CameraCaptureEncode, this },
		std::jthread{ &ClientApp::CameraDecode, this },
	};

	static_assert(thread_count == sizeof(threads) / sizeof(std::jthread));

	ui.video.encode.wait_wh.acquire();

	//assert(thread_count == threads.size());

	glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());
		
	struct imgStruct {
		float pos[3];
		float texCoords[2];
	};

	gl::Texture2D img(1, 1, { 0x33,0x33,0x33,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);

	std::unique_ptr<gl::Texture2D> camTexture;
	int cam_w = 0;
	int cam_h = 0;
	int cam_tex_w = 0;
	int cam_tex_h = 0;
	std::vector<uint8_t> camPixelBuffer;

#if USE_IMGUI
	const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif

	int sc_width{};
	int sc_height{};
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		assert(monitor);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		assert(mode);
		sc_width = mode->width;
		sc_height = mode->height;
	}

	boost::float64_t d_time = 0;
	float fps = 0;
	bool show_settings = true;
	struct {
		char words_msg_buf[sizeof(ui.send.words_msg.buf)]{};
		char name_buf[2 * sizeof(p1.name)]{};
		char rename[sizeof(p1.name)]{};
		uint32_t room_passwd{ Room::invalid_passwd };
	} mid;
	ST::DecodedFrame decodedFrame;


	auto last_time = std::chrono::steady_clock::now();
	auto renderDealing = [&]() {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.video.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

#if USE_IMGUI
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
#endif

		glClearColor(0.1f, 0.1f, 0.1f, 0.1f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//shader.use();
		//vaoImg.bind();
		//gl::draw(eboImg, gl::DrawMode::TRIANGLES);

		DEFER {
			[this]() {window->swapBuffers();}
		};

#if USE_IMGUI
		ImGui::DockingSpace();

		// ============ setting window ============ 
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			show_settings = !show_settings;

	if (show_settings) {
		ImGui::Begin((char*)u8"settings");
		if (ImGui::Button("<-")) {
			p_show.setEnd(true, ShowPara::End::Return);
		}
		if (ImGui::TreeNode("message")) {
			ImGui::PushIdGuard pushIdGuard(__LINE__);
			// real_fps
			ImGui::Text("real_fps: %.3f", get(ui.send.real_fps));
			// fps
			COMP_ATOMIC_T(ImGui::SliderScalar, "fps", ImGuiDataType_Double, ui.send.fps, &MIN_MAX_FPS, &MAX_MAX_FPS);
			// share_video
			COMP_ATOMIC(ImGui::Checkbox, "share_video", ui.send.share_video);
			// share_audio
			COMP_ATOMIC(ImGui::Checkbox, "share_audio", ui.send.share_audio);
			// words_msg
			if (get(ui.send.words_msg_target) == user::public_target) {
				constexpr const char public_target[] = "public";
				memcpy(mid.name_buf, public_target, sizeof(public_target));
			}
			else {
				std::lock_guard lock(mtx_users);
				auto& name = users[get(ui.send.words_msg_target)].name;
				memcpy(mid.name_buf, name.c_str(), name.size() + 1);
			}
			ImGui::Text("message_target: ");
			if (ImGui::BeginCombo("##message_targets", mid.name_buf)) {
				while (true) {
					if (ImGui::Selectable("public")) {
						set(ui.send.words_msg_target, user::public_target);
						break;
					}
					{
						std::lock_guard lock(mtx_users);
						for (const auto& user : users) {
							if (user.first == logger.id) { continue; }
							ImGui::PushU64Guard pushU64Guard(user.first);
							if (ImGui::Selectable(user.second.name.c_str())) {
								set(ui.send.words_msg_target, user.first);
								break;
							}
						}
					}
					break;
				}
				ImGui::EndCombo();
			}// ##message_targets
			// total_words_msg
			ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushTextWrapPos(0.0f);   // 0 表示按窗口右边界换行
			if(ui.send.words_msg_target == user::public_target) {
				std::lock_guard lock{ ui.send.total_words_msg.mtx };
				for (const auto& msg : ui.send.total_words_msg.buf) {
					ImGui::TextUnformatted(msg.c_str());
				}
			}
			else {
				std::lock_guard lock(mtx_users);
				auto& msgs = users[get(ui.send.words_msg_target)].total_words_msg;
				for (const auto& msg : msgs.buf) {
					ImGui::TextUnformatted(msg.c_str());
				}
			}
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			// words_msg
			ImGui::InputText("##message", mid.words_msg_buf, sizeof(mid.words_msg_buf));
			ImGui::SameLine();
			if (ImGui::Button("send")) {
				std::lock_guard lock(ui.send.words_msg.mtx);
				memcpy(ui.send.words_msg.buf, mid.words_msg_buf, sizeof(mid.words_msg_buf));
				mid.words_msg_buf[0] = '\0';
			}

			ImGui::TreePop();
		}// message
		if (ImGui::TreeNode("vedio")) {
			ImGui::PushIdGuard pushIdGuard(__LINE__);
			// fps
			if (d_time > 0.4) {
				fps = ImGui::GetIO().Framerate;
				d_time = 0;
			}
			ImGui::Text("FPS: %.3f", fps);
			COMP_ATOMIC_T(ImGui::SliderScalar, "fps", ImGuiDataType_Double, ui.video.fps, &MIN_MAX_FPS, &MAX_MAX_FPS);
			if (ImGui::TreeNode("encode")) {
				// fps
				COMP_ATOMIC(ImGui::SliderInt, "fps", ui.video.encode.fps, static_cast<int>(MIN_MAX_FPS), static_cast<int>(MAX_MAX_FPS));
				// bitrate
				COMP_ATOMIC(ImGui::SliderInt, "bitrate", ui.video.encode.bitrate, video::min_bitrate, video::max_bitrate);
				// width
				COMP_ATOMIC_EX(ImGui::SliderInt, "width", ui.video.encode.width, [](int v) {return v & ~1;}, static_cast<int>(sc_width * 0.1), sc_width);
				// height
				COMP_ATOMIC_EX(ImGui::SliderInt, "height", ui.video.encode.height, [](int v) {return v & ~1;}, static_cast<int>(sc_height * 0.1), sc_height);
				ImGui::TreePop();
			}// encode
			ImGui::TreePop();
		}// vedio
		if (ImGui::TreeNode("room_user")) {
#			ifndef NDEBUG
				ImGui::Text("id: %zu", (size_t)(get(logger.id)));
				ImGui::Text("socket: %zu", (size_t)(client.Id()));
#			endif
			// name
			ImGui::Text("name: %s", logger.name.c_str());
			// room id
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			// rename
			ImGui::InputText("##rename", mid.rename, sizeof(mid.rename), ImGuiInputTextFlags_CharsNoBlank);
			ImGui::SameLine();
			if (ImGui::Button("rename")) {
				{
					std::lock_guard lock(logger.mtx_name);
					logger.name = mid.rename;
				}
				glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());
			}
			// share_video
			COMP_ATOMIC(ImGui::Checkbox, "share_video", ui.send.share_video);
			// share_camera
			COMP_ATOMIC(ImGui::Checkbox, "share_camera", ui.send.share_camera);
			// share_audio
			COMP_ATOMIC(ImGui::Checkbox, "share_audio", ui.send.share_audio);
			// is_muted_keep
			bool is_muted_keep = ui.room_user.is_muted_keep;
			ImGui::SameLine();
			ImGui::Checkbox("is_muted", &is_muted_keep);
			{
				ImGui::PushIdGuard pushIdGuard(__LINE__);
				std::lock_guard lock(mtx_users);
				auto chosen = users.find(get(ui.room_user.chosen_user));
				assert(chosen != users.end());
				//auto name_chosen = users[chosen->first].name + (get(users[chosen->first].is_host) ? "(host)" : "");
				auto& chosen_user = users[chosen->first];
				constexpr char host_signal[] = "(host)";
				memcpy(mid.name_buf, chosen_user.name.c_str(), chosen_user.name.size() + 1);
				if (get(chosen_user.is_host)) {
					memcpy(mid.name_buf + chosen_user.name.size(), &host_signal, sizeof(host_signal));
				}
				ImGui::Text("target:");
				if (ImGui::TreeNode("target", mid.name_buf)) {
					const bool is_host = isHost<false>();
					if (ImGui::BeginTable("##target_users", is_host ? 6 : 4, ImGuiTableFlags_SizingFixedFit)) {
						ImGui::TableSetupColumn("target");
						ImGui::TableSetupColumn("video");
						ImGui::TableSetupColumn("audio");
						ImGui::TableSetupColumn("aloud");
						if (is_host) {
							ImGui::TableSetupColumn("kick_off");
							ImGui::TableSetupColumn("transfer_host");
						}
						ImGui::TableHeadersRow();
						for (auto& user : users) {
							ImGui::PushU64Guard pushU64Guard(user.first);

							const size_t name_len = user.second.name.size();
							memcpy(mid.name_buf, user.second.name.c_str(), name_len + 1);
							size_t name_buf_size = user.second.name.size();
							if (get(user.second.is_host)) {
								memcpy(mid.name_buf + name_len, &host_signal, sizeof(host_signal));
								name_buf_size += sizeof(host_signal);
							}

							// select user
							ImGui::TableNextColumn();
							if (ImGui::Selectable(mid.name_buf)) {
								set(ui.room_user.chosen_user, user.first);
							}
							// share_video
							bool v = get(user.second.share_video);
							ImGui::TableNextColumn();
							ImGui::Checkbox("##video", &v);
							// share_audio
							v = get(user.second.share_audio);
							ImGui::TableNextColumn();
							ImGui::Checkbox("##audio", &v);
							// audio_playing
							constexpr const char audio_playing[] = ">))";
							constexpr const char no_audio_playing[sizeof(audio_playing)] = "___";
							ImGui::TableNextColumn();
							if (get(user.second.audio_playing)) {
								ImGui::Text(audio_playing);
							}
							else {
								ImGui::Text(no_audio_playing);
							}

							if (is_host) {
								ImGui::TableNextColumn();
								// kick off
								if (!user.second.is_host) {
									if (ImGui::Button("kick_off")) {
										set(ui.room_user.host.wanna_kick_off, user.first);
									}
								}
								else {
									ImGui::Button("(host)");
								}
								// transfer
								ImGui::TableNextColumn();
								if (ImGui::Button("transfer")) {
									set(ui.room_user.host.transfer_host, user.first);
								}
							}
						}
						ImGui::EndTable();
					}
					ImGui::TreePop();
				}
			}

			ImGui::TreePop();
		}// room_user

		if (isHost<true>() && ImGui::TreeNode("host")) {
			uint32_t const_room_passwd = get(ui.room_user.host.room_passwd);
			// mute_all_once 
			bool mute_all_once = get(ui.room_user.host.mute_all_once);
			mute_all_once |= ImGui::Button("mute_all_once");
			set(ui.room_user.host.mute_all_once, mute_all_once);
			// mute_all_keep
			COMP_ATOMIC(ImGui::Checkbox, "mute_all_keep", ui.room_user.host.mute_all_keep);
			// room_id
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			// room_passwd
			ImGui::InputScalar("room_passwd", ImGuiDataType_U32, &const_room_passwd);
			ImGui::InputScalar("##set_room_passwd", ImGuiDataType_U32, &mid.room_passwd);
			ImGui::SameLine();
			if (ImGui::Button("reset_passwd")) {
				set(ui.room_user.host.set_room_passwd, mid.room_passwd);
				set(ui.room_user.host.room_passwd, mid.room_passwd);
			}
			if (ImGui::TreeNode("kick_off", "kick_off_target")) {
				{
					std::lock_guard lock(mtx_users);
					for (const auto& user : users) {
						ImGui::PushU64Guard pushU64Guard(user.first);
						if (get(user.second.is_host)) {
							continue;
						}
						if (ImGui::Selectable(user.second.name.c_str())) {
							set(ui.room_user.host.wanna_kick_off, user.first);
							println("kick off user_id: " << user.first);
						}
					}
				}
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}// host

		if (ImGui::TreeNode("plugin")) {
			ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushTextWrapPos(0.0f);   // 0 表示按窗口右边界换行
			{
				std::lock_guard lock(mtx_speechWords);
				ImGui::TextUnformatted(speechWords.c_str());
			}
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			ImGui::TreePop();
		}// plugin

		ImGui::End();
	}// settings


		// ============ main window ============ 
		style->Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
		ImGui::Begin("main");
		{
			const ImVec2 winSize = ImGui::GetContentRegionAvail();
			const ImVec2 content_origin = ImGui::GetCursorScreenPos();
			const auto imgSize = img.imgSize();
			ImVec2 show_img_size;

			float offsetX{};
			float offsetY{};
			const float img_w_y = imgSize.x / imgSize.y;
			const float win_w_y = winSize.x / winSize.y;
			if (img_w_y > win_w_y) {
				offsetX = 0.0f;
				show_img_size.x = winSize.x;
				show_img_size.y = show_img_size.x / img_w_y;
				offsetY = (winSize.y - show_img_size.y) / 2;
			}
			else {
				offsetY = 0.0f;
				show_img_size.y = winSize.y;
				show_img_size.x = show_img_size.y * img_w_y;
				offsetX = (winSize.x - show_img_size.x) / 2;
			}
			ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
			ImGui::Image(img.id(), show_img_size);
		}
		ImGui::End();// main

		user::id_t target_id = get(ui.room_user.chosen_user);
		bool target_share_camera = false;

		if (target_id == get(logger.id)) {
			// 如果选中的是自己，看自己的开关
			target_share_camera = get(ui.send.share_camera);
			// 如果自己关了摄像头，关闭摄像头采集
			if (!target_share_camera && m_cameraCapture.IsOpened()) {
				EnableCamera(false);
			}
		}
		else {
			// 如果选中的是别人，从网络同步的状态里看对方开没开
			std::lock_guard lock(mtx_users);
			auto it = users.find(target_id);
			if (it != users.end()) {
				target_share_camera = it->second.share_camera;
			}
		}

		ST::DecodedFrame cam_frame;
		if (total_camera_frames.pop(cam_frame)
			&& cam_frame.width > 0 && cam_frame.height > 0
			&& cam_frame.rgba.size() == (size_t)cam_frame.width * cam_frame.height * 4)
		{
			cam_w = cam_frame.width;
			cam_h = cam_frame.height;

			if (!camTexture || cam_tex_w != cam_w || cam_tex_h != cam_h) {
				cam_tex_w = cam_w;
				cam_tex_h = cam_h;

				camTexture.reset(new gl::Texture2D(
					cam_w, cam_h, cam_frame.rgba,
					gl::GpuTextureFmt::RGBA8, gl::TextureFmt::RGBA, 4
				));
			}
			else {
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, camTexture->id());

				camTexture->resetData(cam_frame.rgba, 4);
			}
		}

		if (!target_share_camera && camTexture) {
			camTexture.reset();
			cam_tex_w = 0;
			cam_tex_h = 0;
		}

		if (target_share_camera && camTexture && cam_tex_w > 0 && cam_tex_h > 0) {

			ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(sc_width - 340, 20), ImGuiCond_FirstUseEver);
			ImGui::Begin("camera");

			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float tex_w_y = static_cast<float>(cam_tex_w) / cam_tex_h;
			const float win_w_y = avail.x / avail.y;
			ImVec2 show_size;
			if (tex_w_y > win_w_y) {
				show_size.x = avail.x;
				show_size.y = show_size.x / tex_w_y;
			}
			else {
				show_size.y = avail.y;
				show_size.x = show_size.y * tex_w_y;
			}
			ImGui::SetCursorPos(ImVec2(
				(avail.x - show_size.x) * 0.5f,
				(avail.y - show_size.y) * 0.5f));

			ImGui::Image(camTexture->id(), show_size, ImVec2(0, 1), ImVec2(1, 0));


			ImGui::End();
		}

		style->Colors[ImGuiCol_WindowBg] = DEFAULT_BK_COLOR;
		// ============ main window ============ 

		ImGui::End();//docking end
		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if USE_IMGUI_VIEWPORT
		{
			GLFWwindow* backup = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup);   // 恢复主上下文
		}
#endif// USE_IMGUI_VIEWPORT

		d_time += 1 / (boost::float64_t)ImGui::GetIO().Framerate;
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));


		if (!total_video_frames_v1.empty()) {
			total_video_frames_v1.pop(decodedFrame);
		}

		int width = decodedFrame.width;
		int height = decodedFrame.height;
		auto& data = decodedFrame.rgba;

		if (width > 0 && height > 0 && data.size() == (size_t)width * height * 4) {
			bool share_video;
			{
				std::lock_guard lock(mtx_users);
				share_video = users[get(ui.room_user.chosen_user)].share_video;
			}
			
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, img.id());

			if (share_video) {
				img.resize(nullptr, width, height, 4);
				img.resetData(data, 4);
			}
			else {
				img.resize(nullptr, 1, 1, 4);
				img.resetData({ 0,0,0,0 }, 4);
			}
		}
	};
	any_usage = &renderDealing;
	using renderFuncType = decltype(renderDealing);
	glfwSetWindowUserPointer(window->m_get, this);
	glfwSetWindowPosCallback(window->m_get, [](GLFWwindow* window, int xpos, int ypos) {
		auto user = static_cast<ClientApp*>(glfwGetWindowUserPointer(window));
		user->window->updatePos({ xpos, ypos });
		auto renderDealing = *static_cast<renderFuncType*>(user->any_usage);
		renderDealing();
		});
	glfwSetFramebufferSizeCallback(window->m_get, [](GLFWwindow* window, int width, int height) {
		auto user = static_cast<ClientApp*>(glfwGetWindowUserPointer(window));
		glViewport(0, 0, width, height);
		user->window->setViewport({ 0,0,width, height });
		auto renderDealing = *static_cast<renderFuncType*>(user->any_usage);
		renderDealing();
		});
	while (!p_show.ended() && !clientClosed()) {
		if (window->shouldClose()) [[unlikely]] { 
			p_show.setEnd(true, ShowPara::End::Default); 
			set(close_signal, true);  
			return Page::Tmp; 
		}
		glfwPollEvents();
		renderDealing();
	}

	println("show ended");
	switch (p_show.showEnd()) {
	case ShowPara::End::clientClosed:
		return makeTmp("clientClosed", "your tcp socket closed in the middle.", Page::connectToServer);
		break;
	case ShowPara::End::kickOff:
		return makeTmp("kickOff", "you are kicked off by the room host.", Page::connectToServer);
		break;
	case ShowPara::End::Return:
		return Page::connectToServer;
		break;
	default:
		assert(false);
	}
	return Page::Tmp;
}

ClientApp::Page ClientApp::Tmp() {
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
		PageRenderGuard pageRenderGuard{ this, p_tmp.title.c_str(), 20};
		ImGui::Text(p_tmp.content.c_str());
		if (ImGui::Button("<-")) { break; }
	}
	return p_tmp.back;
}

ClientApp::Page ClientApp::makeTmp(std::string title, std::string content, Page back) {
	p_tmp.title = std::move(title);
	p_tmp.content = std::move(content);
	p_tmp.back = back;
	return Page::Tmp;
}

void ClientApp::pageRenderBegin(const char* title, int sleepTime) {
	std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
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

void ClientApp::pageRenderEnd() {
#if USE_IMGUI
	ImGui::End();// settings
	ImGui::End();//docking end
	// Rendering
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if	USE_IMGUI_VIEWPORT
	GLFWwindow* backup = glfwGetCurrentContext();
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
	glfwMakeContextCurrent(backup);   // 恢复主上下文
#endif// USE_IMGUI_VIEWPORT
#endif	
	window->swapBuffers();
}

ClientApp::Page ClientApp::chooseMode() {
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
		PageRenderGuard pageRenderGuard{ this, "choose mode", 20 };
		if (ImGui::Button("<-")) { client.Send(true); return Page::connectToServer; }
		if (ImGui::Button("enter a room")) {
			client.Send(false);
			client.Send(choice_enter_room);
			return Page::enterRoom;
		}
		if (ImGui::Button("make a room")) {
			client.Send(false);
			client.Send(choice_make_room);
			return Page::makeRoom;
		}
	}
	return Page::Tmp;
}

ClientApp::Page ClientApp::makeRoom() {
	logger.room_id = room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	while (1) {
		while (1) {
			if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
			PageRenderGuard PageRenderGuard{ this, "passwd", 20 };
			if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
			ImGui::InputScalar("passwd", ImGuiDataType_U32, &p2.passwd);
			if (p2.passwd == Room::invalid_passwd) {
				ImGui::Text("invalid passwd: %d", (int)Room::invalid_passwd);
			}
			else if(ImGui::Button("->")) { break; }
		}
		logger.passwd = p2.passwd;
		client.Send(false);
		client.Send(logger.passwd);
		uint32_t room_id0{};
		if (!client.ReceiveBy(room_id0)) {
			return makeTmpServerStatusError();
		}
		if (room_id0 == Room::invalid_passwd) {
			return makeTmp("audio connection error", "Audio connection error happened while making room. Please try reconnect", Page::connectToServer);
		}
		logger.room_id = room_id0;
		return Page::Show;
	}
}

ClientApp::Page ClientApp::enterRoom() {
	logger.room_id = room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	bool room_id_not_exist{ false };
	bool passwd_wrong{ false };
	bool room_cleared{ false };
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
		PageRenderGuard pageRenderGuard(this, "enter room", 20);
		if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
		ImGui::InputScalar("room_id", ImGuiDataType_U32, &p3.room_id);
		ImGui::InputScalar("passwd", ImGuiDataType_U32, &p3.passwd);
		ImGui::Text("both: 1~4294967295");
		bool want_continue{ false };
		if (p3.room_id == room::invalid_id)		{ ImGui::Text("room_id format invalid"); want_continue = true; }
		if (p3.passwd == Room::invalid_passwd)	{ ImGui::Text("passwd format invalid"); want_continue = true; }
		if (want_continue) { continue; }
		if (room_id_not_exist)	{ ImGui::Text("room id not exist"); } 
		if (passwd_wrong)		{ ImGui::Text("passwd wrong"); }
		if (room_cleared)		{ ImGui::Text("others in the room all left while you're entering"); }
		if (ImGui::Button("->")) {
			client.Send(false);
			client.Send(p3.room_id);
			client.Send(p3.passwd);
			bool room_id_ok{};
			if (!client.ReceiveBy(room_id_ok))	{ return makeTmpServerStatusError(); }
			if (!room_id_ok)					{ room_id_not_exist = true; continue; }
			else { room_id_not_exist = false; }
			bool passwd_ok{};
			if (!client.ReceiveBy(passwd_ok))	{ return makeTmpServerStatusError(); }
			if (!passwd_ok)						{ passwd_wrong = true; continue; }
			else { passwd_wrong = false; }
			//bool sync_audio_connection;
			//if(!client_audio.ReceiveBy(sync_audio_connection)) {  
			//	return makeTmp("audio connection error", "Audio connection error happened while entering room. Please try reconnect", Page::connectToServer);
			//}
			bool enter_ok;
			if (!client.ReceiveBy(enter_ok))	{ return makeTmpServerStatusError(); }
			if (!enter_ok)						{ room_cleared = !enter_ok; continue; }
			else { room_cleared = false; }
			break;
		}
	}
	logger.passwd = p3.passwd;
	logger.room_id = p3.room_id;
	return Page::Show;
}

ClientApp::Page ClientApp::connectToServer() {
	logger.name = {};

	user::id_t get_id;
	bool first{ true };
	loop {
		client = net::tcp::Client{ net::tcp::Ip::v4 };
		client_audio = net::tcp::Client{ net::tcp::Ip::v4 };
		loop {
			if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
			PageRenderGuard pageRenderGuard{this, "connect to server", 20 };
			ImGui::Text("your tcp socket: %zu", static_cast<size_t>(client.Id()));
			ImGui::InputText("target_ipv4", p1.ipv4, sizeof(p1.ipv4));
			ImGui::InputScalar("target_port", ImGuiDataType_U16, &p1.port);
			ImGui::InputText("your_name", p1.name, sizeof(p1.name), ImGuiInputTextFlags_CharsNoBlank);
			bool ok{ true };
			if (!first)						{ ImGui::Text("connect failed"); } 
			if (strlen(p1.name) == 0)		{ ImGui::Text("name should not be blank"); ok = false; }
			if (p1.port == p1.invalid_port)	{ ImGui::Text("%s: port invalid - should be a unsigned number not 0", p1.port); ok = false; }
			if (ok && ImGui::Button("connect")) { break; }
		}
		bool sync_connection;
		if (client.ConnectTo(net::tcp::Ip::v4, p1.ipv4, p1.port) &&
			client.Send(connect_user) &&
			client.ReceiveBy(get_id) &&

			client_audio.ConnectTo(net::tcp::Ip::v4, p1.ipv4, p1.port) &&
			client_audio.Send(connect_audio) &&
			client_audio.Send(get_id) &&
			client_audio.ReceiveBy(sync_connection) &&
			client.Send(sync_connection)
		) {
			println(p1.ipv4);
			println(p1.port);
			println("connected to server successfully");
			break;
		}
		else {
			println("connected to server failed");
			first = false;
		}
	}
	logger.name = p1.name;

	set(logger.id, get_id);
	println("id: " << logger.id);
	println("socket: " << client.Id());
	println("name: " << logger.name);
	return Page::chooseMode;
}

void ClientApp::run() {
#ifdef NDEBUG
	av_log_set_level(AV_LOG_ERROR); // ffmpeg log
#endif//NDEBUG

	while (!get(close_signal)) {
		switch (page) {
		case Page::connectToServer: page = connectToServer(); break;
		case Page::chooseMode: page = chooseMode(); break;
		case Page::makeRoom: page = makeRoom(); break;
		case Page::enterRoom: page = enterRoom(); break;
		case Page::Show: page = Show(); break;
		case Page::Tmp: page = Tmp(); break;
		}
	}

}

ClientApp::ClientApp() {

#ifdef _WIN32
	timeBeginPeriod(1);   // ClientApp::App() 里，配合 ClientApp::~App() 的 timeEndPeriod(1)
#endif


#if USE_IMGUI
	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	io = &ImGui::GetIO();
	io->ConfigFlags =
#if USE_IMGUI_VIEWPORT
		ImGuiConfigFlags_ViewportsEnable |
#endif// USE_IMGUI_VIEWPORT
		ImGuiConfigFlags_DockingEnable |
		ImGuiConfigFlags_NavEnableKeyboard |     // Enable Keyboard Controls
		ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	ImGui::StyleColorsDark();

	// Setup scaling
	style = &ImGui::GetStyle();
	style->ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style->FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

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
#endif

	bool ret{};
	ret = plug.asr.loader.load("plugin/plugin_ASR.dll");
	assert(ret);
	plug.asr.loader.get(plug.asr.init, plugin::name::asrInit);
	plug.asr.loader.get(plug.asr.transcribe, plugin::name::asrTranscribe);
	plug.asr.loader.get(plug.asr.free, plugin::name::asrFree);
	ret = plug.asr.init("plugin/ggml-tiny.bin") == 0;
	assert(ret);


	loadUiState();
}

ClientApp::~ClientApp() {
	m_cameraCapture.Close();

	storeUiState();

	plug.asr.free();

#if USE_IMGUI

	// Cleanup
	ImGui::DestroyPlatformWindows();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
#endif

#ifndef NDEBUG
	println("end ok(press enter to finish)");
	//(void)getchar();
#endif

#ifdef _WIN32
	timeEndPeriod(1);   // ClientApp::App() 里，配合 ClientApp::~App() 的 timeEndPeriod(1)
#endif
}

int main() {
	auto app = std::make_unique<ClientApp>();
	app->run();
}