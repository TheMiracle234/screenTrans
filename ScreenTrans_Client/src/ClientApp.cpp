// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#define _CRT_SECURE_NO_WARNINGS
#include <ClientApp.hpp>

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
#include <chrono>
#include <condition_variable>

// 正在后台收尾的 summary 实例数。summaryLoad 会等它归零，
// 避免两个实例同时操作 DLL 里的全局 AiLauncher。
static std::mutex g_summary_teardown_mtx;
static std::condition_variable g_summary_teardown_cv;
static int g_summary_teardown = 0;

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

template<bool enable>
void ClientApp::EnableCamera() {
    if constexpr (enable) {
        if(!m_cameraCapture.IsOpened()) {
            bool ret;
            {
                std::lock_guard lock(mtx_camera);
                ret = m_cameraCapture.Open();
            }
            if (ret) [[likely]] {
                //set(ui.send.share_camera, true);
                std::cout << "摄像头开启成功" << "\n";
            }
            else {
                //set(ui.send.share_camera, false);
                std::cout << "摄像头开启失败" << "\n";
            }
            ST::DecodedFrame dummy;
            while (total_capture_frames.pop(dummy)) {}
            while (send_camera_packets.pop(dummy.rgba)) {}
        }
    }
    else {
        if(m_cameraCapture.IsOpened()) {
            m_cameraCapture.Close();
        }
        //set(ui.send.share_camera, false);
    }
}

void ClientApp::CameraCaptureThread() {
    //DEFER{
    //	[this]() {p_show.wait_threads.release(); }
    //};

    EnableCamera<true>();
    cam_ok = m_cameraCapture.IsOpened();
    cam_sync0.release();
    cam_sync1.acquire();
    EnableCamera<false>();


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
            EnableCamera<true>();
        }
        
        if (!wantCamera && m_cameraCapture.IsOpened()) {
            EnableCamera<false>();
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

    EnableCamera<false>();
}
//发送转码
void ClientApp::CameraCaptureEncode() {
    //DEFER{
    //	[this]() {p_show.wait_threads.release(); }
    //};
    ST::DecodedFrame frame;
    int w = 1, h = 1;
    util::Timer timer;
    cam_sync0.acquire();
    if (cam_ok) {
        std::vector<uint8_t> camBuf;
        int camW = 0, camH = 0;
        while (!p_show.ended() && !clientClosed()) {			
            if (timer.seconds() > 2.0) { break; }
            bool camHasFrame = m_cameraCapture.GetFrame(camBuf, camW, camH);
            if (camHasFrame) {
                frame.width = camW;
                frame.height = camH;
                frame.rgba.swap(camBuf);
                break;
            }
        }
        w = frame.width & ~1;
        h = frame.height & ~1;
    }
    cam_sync1.release();
    set(ui.camera.encode.width, w);
    set(ui.camera.encode.height, h);
    ui.camera.encode.wait_wh.release(2);




    ST::H264Encoder encoder(w, h, get(ui.camera.encode.fps), get(ui.camera.encode.bitrate));
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

        encoder.Reset(w, h, ui.camera.encode.width, ui.camera.encode.height, ui.camera.encode.fps, ui.camera.encode.bitrate);
        set(ui.camera.encode.width, encoder.Width());
        set(ui.camera.encode.height, encoder.Height());

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


void ClientApp::storeUiState() {
    std::FILE* const file = std::fopen("FlexMeet.ini", "wb");
    DEFER([file]() { std::fclose(file); });
    assert(file);
    Ui buf_ui;

    constexpr size_t BEGIN = __LINE__ + 1; // === BEGIN - 1 ===
    memcpy(buf_ui.p1.ipv4,						p1.ipv4, sizeof(p1.ipv4));//
    buf_ui.p1.port =							net::host_to_network(p1.port);
    memcpy(buf_ui.p1.name,						p1.name, sizeof(p1.name));
    buf_ui.p2.passwd =							net::host_to_network(p2.passwd);//
    buf_ui.p3.room_id =							net::host_to_network(p3.room_id);//
    buf_ui.p3.passwd =							net::host_to_network(p3.passwd);//
    buf_ui.send.fps =							net::host_to_network(get(ui.send.fps));//
    buf_ui.send.share_audio =					net::host_to_network(get(ui.send.share_audio));
    buf_ui.send.share_video =					net::host_to_network(get(ui.send.share_video));
    buf_ui.send.share_camera =					net::host_to_network(get(ui.send.share_camera));
    buf_ui.video.fps =							net::host_to_network(get(ui.video.fps));//
    buf_ui.video.encode.fps =					net::host_to_network(get(ui.video.encode.fps));
    buf_ui.video.encode.bitrate =				net::host_to_network(get(ui.video.encode.bitrate));
    buf_ui.video.encode.width =					net::host_to_network(get(ui.video.encode.width));
    buf_ui.video.encode.height =				net::host_to_network(get(ui.video.encode.height));
    buf_ui.camera.encode.fps =					net::host_to_network(get(ui.camera.encode.fps));
    buf_ui.camera.encode.bitrate =				net::host_to_network(get(ui.camera.encode.bitrate));
    buf_ui.camera.encode.width =				net::host_to_network(get(ui.camera.encode.width));
    buf_ui.camera.encode.height =				net::host_to_network(get(ui.camera.encode.height));
    buf_ui.room_user.host.set_room_passwd =		net::host_to_network(get(ui.room_user.host.set_room_passwd));
    buf_ui.room_user.host.mute_all_keep =		net::host_to_network(get(ui.room_user.host.mute_all_keep));
    buf_ui.room_user.host.allow_asr =			net::host_to_network(get(ui.room_user.host.allow_asr));
    buf_ui.room_user.host.allow_translator =	net::host_to_network(get(ui.room_user.host.allow_translator));
    buf_ui.room_user.host.allow_summary =		net::host_to_network(get(ui.room_user.host.allow_summary));
    constexpr size_t END = __LINE__; // === END ===
    static_assert(END - BEGIN == count_basic_members<Ui>());

    std::fwrite(&buf_ui, sizeof(buf_ui), 1, file);
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
    memcpy( p1.name,							buf_ui.p1.name, sizeof(p1.name));
            p2.passwd =							net::network_to_host(buf_ui.p2.passwd);//
            p3.room_id =						net::network_to_host(buf_ui.p3.room_id);//
            p3.passwd =							net::network_to_host(buf_ui.p3.passwd);//
    set(	ui.send.fps,						net::network_to_host(buf_ui.send.fps));//
    set(	ui.send.share_audio,				net::network_to_host(buf_ui.send.share_audio));
    set(	ui.send.share_video,				net::network_to_host(buf_ui.send.share_video));
    set(	ui.send.share_camera,				net::network_to_host(buf_ui.send.share_camera));
    set(	ui.video.fps,						net::network_to_host(buf_ui.video.fps));//
    set(	ui.video.encode.fps,				net::network_to_host(buf_ui.video.encode.fps));
    set(	ui.video.encode.bitrate,			net::network_to_host(buf_ui.video.encode.bitrate));
    set(	ui.video.encode.width,				net::network_to_host(buf_ui.video.encode.width));
    set(	ui.video.encode.height,				net::network_to_host(buf_ui.video.encode.height));
    set(	ui.camera.encode.fps,				net::network_to_host(buf_ui.camera.encode.fps));
    set(	ui.camera.encode.bitrate,			net::network_to_host(buf_ui.camera.encode.bitrate));
    set(	ui.camera.encode.width,				net::network_to_host(buf_ui.camera.encode.width));
    set(	ui.camera.encode.height,			net::network_to_host(buf_ui.camera.encode.height));
    set(	ui.room_user.host.set_room_passwd,	net::network_to_host(buf_ui.room_user.host.set_room_passwd));
    set(	ui.room_user.host.mute_all_keep,	net::network_to_host(buf_ui.room_user.host.mute_all_keep));
    set(	ui.room_user.host.allow_asr,		net::network_to_host(buf_ui.room_user.host.allow_asr));
    set(	ui.room_user.host.allow_translator,	net::network_to_host(buf_ui.room_user.host.allow_translator));
    set(	ui.room_user.host.allow_summary,	net::network_to_host(buf_ui.room_user.host.allow_summary));
    constexpr size_t END = __LINE__; // === END ===
    static_assert(END - BEGIN == count_basic_members<Ui>());
}

bool ClientApp::clientClosed() {
    std::lock_guard lock(mtx_close);
    return client.Closed();
}

void ClientApp::Receive() {
    //DEFER{ [this]() {
    //	p_show.wait_threads.release();
    //}};

    struct {
        s_Msg fixed;
        //Signal signals{};
        //user::id_t id{ user::invalid_id };
        //char other_name[name_bytes];
        //bool is_host_from_server{};
        //uint32_t room_passwd{};
        //s_HostPower hostPower{
        //	.is_host_from_client{},
        //	.allow_asr{},
        //	.allow_translator{},
        //	.wanna_kick_off{},
        //};
        //bool has_words_msg{};
        //user::id_t words_msg_target{};
        //char words_msg[words_msg_bytes];
        //bool share_video{};
        //int32_t pk_size{};
        //int32_t video_w;
        //int32_t video_h;
        std::vector<uint8_t> pk{};

        //bool share_camera{};
        //int32_t cam_pk_size{};
        //int32_t cam_w;
        //int32_t cam_h;
        std::vector<uint8_t> cam_pk{};

        void restore() {
        }
    } rv;

    while (!p_show.ended() && !clientClosed()) {
        rv.restore();
        //bool recv_ok{true};
        if (!client.ReceiveBy(&rv.fixed, sizeof(rv.fixed))) {
            break; 
        }
        s_MsgNtoh(rv.fixed);

        //if (!client.ReceiveBy(rv.signals) ||  
        //	!client.ReceiveBy(rv.id)) [[unlikely]] 
        //{
        //	break;
        //}
        if (rv.fixed.signal & signal_closed) [[unlikely]] {
            {
                std::lock_guard lock_chosen{ ui.room_user.mtx_chosen_users };
                std::lock_guard lock_users(mtx_users);
                users.erase(rv.fixed.id);
                //auto count = std::erase_if(users, [&](auto& user) { return user.first == rv.id;});

                ui.room_user.chosen_users.erase(rv.fixed.id);
                if (ui.room_user.chosen_users.empty()) {
                    ui.room_user.chosen_users.emplace(getx(logger.id), std::make_shared<RecvDataEx>(getx(logger.id)));
                }
            }
            //set(ui.room_user.chosen_user, get(logger.id)); // ### 遍历选择用户，若存在rv.id{ 若只有1个，则换成logger.id，否则erase }
            continue;
        }

        //client.ReceiveBy(rv.other_name, sizeof(rv.other_name));
        //if (!client.ReceiveBy(rv.is_host_from_server)) [[unlikely]] { break; }
        if (rv.fixed.is_host_from_server && get(logger.id) == rv.fixed.id) {
            //if (!client.ReceiveBy(rv.room_passwd)) [[unlikely]] { break; }
            set(ui.room_user.host.room_passwd, rv.fixed.passwd);
        }
        //if (!client.ReceiveBy(&rv.hostPower, sizeof(rv.hostPower))) { break; }
        //net::recursive_apply(rv.hostPower, net::NetworkToHost{});
        //if (!client.ReceiveBy(rv.is_host_from_client)) [[unlikely]] { break; }
        //if (rv.is_host_from_client) {
        //	client.ReceiveBy(rv.wanna_kick_off);
        //}

        //if (!client.ReceiveBy(rv.has_words_msg)) [[unlikely]] { break; }
        if (rv.fixed.has_words_msg) [[unlikely]] {
            //client.ReceiveBy(rv.words_msg_target);
            if (rv.fixed.words_msg_target == user::public_target) {
                //client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
                {
                    std::lock_guard lock0(mtx_users);
                    std::lock_guard lock1(ui.send.total_words_msg.mtx);
                    ui.send.total_words_msg.buf.push_back(users[rv.fixed.id].name + ": " + rv.fixed.words_msg.buf);
                }
            }
            else if (rv.fixed.words_msg_target == logger.id) {
                //client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
                {
                    std::lock_guard lock(mtx_users);
                    users[rv.fixed.id].total_words_msg.buf.push_back(users[rv.fixed.id].name + ": " + rv.fixed.words_msg.buf);
                }
            }
        }

        //if (!client.ReceiveBy(rv.share_video)) [[unlikely]] { break; }

        if (rv.fixed.is_host_from_client) {
            if (rv.fixed.wanna_kick_off == get(logger.id)) [[unlikely]] {
                p_show.setEnd(true, ShowPara::End::kickOff);
            }
            set(ui.plugin.allow_asr, rv.fixed.allow_asr);
            set(ui.plugin.allow_translator, rv.fixed.allow_translator);
        }
        {
            std::lock_guard lock(mtx_users);
            auto [itr, first] = users.try_emplace(rv.fixed.id);
            itr->second.name = rv.fixed.name.buf;
            auto& user = itr->second;
            set(user.is_host, rv.fixed.is_host_from_server);
            set(user.share_video, rv.fixed.share_video);
            set(user.share_camera, rv.fixed.share_camera);
        }

        if (rv.fixed.signal & signal_choiceNotMatch) { continue; }

        // each packet
        if (rv.fixed.share_video) {
            //if (!client.ReceiveBy(rv.pk_size)) [[unlikely]] { break; }
            //client.ReceiveBy(rv.video_w);
            //client.ReceiveBy(rv.video_h);
            {
                std::lock_guard lock_chosen{ ui.room_user.mtx_chosen_users };
                auto chosen_user = ui.room_user.chosen_users.find(rv.fixed.id);
                if (chosen_user != ui.room_user.chosen_users.end()) {
                    set(chosen_user->second->rv_sc_orig_w, rv.fixed.sc_w);
                    set(chosen_user->second->rv_sc_orig_h, rv.fixed.sc_h);
                }
            }
            //const net::EndSrcTar endSrcTar{ rv.fixed.end,  net::End::native };
            for (int i = 0;i < rv.fixed.pk_size;++i) {
                if (!client.ReceiveBy(rv.pk, { rv.fixed.end,  net::End::native })) [[unlikely]] { break; }
                {
                    std::lock_guard lock{ ui.room_user.mtx_chosen_users };
                    auto chosen_user = ui.room_user.chosen_users.find(rv.fixed.id);
                    if (chosen_user != ui.room_user.chosen_users.end()) {
                        chosen_user->second->recv_screen_packets.push(std::move(rv.pk));
                    }
                    //recv_screen_packets.push(std::move(rv.pk));
                }
            }
        }

        //if (!client.ReceiveBy(rv.share_camera)) [[unlikely]] { break; }
        //{
        //	std::lock_guard lock(mtx_users);
        //	set(users[rv.fixed.id].share_camera, rv.fixed.share_camera);
        //}
        if (rv.fixed.share_camera) {
            //if (!client.ReceiveBy(rv.cam_pk_size)) [[unlikely]] { break; }
            //client.ReceiveBy(rv.cam_w);
            //client.ReceiveBy(rv.cam_h);
            //set(rv_cam_orig_w, rv.cam_w);
            //set(rv_cam_orig_h, rv.cam_h);
            {
                std::lock_guard lock_chosen{ ui.room_user.mtx_chosen_users };
                auto chosen_user = ui.room_user.chosen_users.find(rv.fixed.id);
                if (chosen_user != ui.room_user.chosen_users.end()) {
                    set(chosen_user->second->rv_cam_orig_w, rv.fixed.cam_w);
                    set(chosen_user->second->rv_cam_orig_h, rv.fixed.cam_h);
                }
            }
            //const net::EndSrcTar endSrcTar{
            //	rv.signals & signal_littleEnd ? net::End::little : net::End::big,
            //	net::End::native
            //};
            for (int i = 0; i < rv.fixed.cam_pk_size; i++) {
                if (!client.ReceiveBy(rv.cam_pk, { rv.fixed.end, net::End::native })) [[unlikely]] { break; }
                {
                    std::lock_guard lock{ ui.room_user.mtx_chosen_users };
                    auto chosen_user = ui.room_user.chosen_users.find(rv.fixed.id);
                    if (chosen_user != ui.room_user.chosen_users.end()) {
                        chosen_user->second->recv_camera_packets.push(std::move(rv.cam_pk));
                    }
                }
                //recv_camera_packets.push(std::move(rv.cam_pk));
            }
        }
    }
}

void ClientApp::Send() {
    //DEFER{
    //	[this]() {p_show.wait_threads.release();}
    //};

    ui.video.encode.wait_wh.acquire();
    const int32_t sc_w = ui.video.encode.width;
    const int32_t sc_h = ui.video.encode.height;
    ui.camera.encode.wait_wh.acquire();
    const int32_t cam_w = ui.camera.encode.width;
    const int32_t cam_h = ui.camera.encode.height;

    std::vector<user::id_t> buf_chosen_users;

    s_Msg clientSend;

    util::Timer timer;
    timer.restart();
    double sum_time{};
    auto last_time = std::chrono::steady_clock::now();
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

        clientSend.end = net::End::native;
        clientSend.id = get(logger.id);

        //client.Send(net::End::native == net::End::little);
        //client.Send(get(logger.id));
        {
            std::lock_guard lock(logger.mtx_name);
            //const auto size = logger.name.size();
            //logger.name.resize(name_bytes);
            memcpy(clientSend.name.buf, logger.name.data(), sizeof(logger.name.data() + 1));
            //client.Send(logger.name.data(), logger.name.size());
            //logger.name.resize(size);
        }
        clientSend.is_host_from_client = is_host_from_client;
        clientSend.allow_asr = get(ui.room_user.host.allow_asr);
        clientSend.allow_translator = get(ui.room_user.host.allow_translator);
        clientSend.passwd = get(ui.room_user.host.set_room_passwd);
        clientSend.wanna_kick_off = get(ui.room_user.host.wanna_kick_off);
        clientSend.transfer_host = get(ui.room_user.host.transfer_host);
        set(ui.room_user.host.set_room_passwd, Room::invalid_passwd);
        set(ui.room_user.host.wanna_kick_off, user::invalid_id);
        set(ui.room_user.host.transfer_host, user::invalid_id);
        //net::recursive_apply(hostPower, net::HostToNetwork{});
        //client.Send(&hostPower, sizeof(hostPower));
        bool has_words_msg;
        {
            std::lock_guard lock(ui.send.words_msg.mtx);
            has_words_msg = ui.send.words_msg.buf[0] != '\0';
        }
        clientSend.has_words_msg = has_words_msg;
        //client.Send(has_words_msg);
        if (has_words_msg) [[unlikely]] {
            user::id_t target = get(ui.send.words_msg_target);
            char buf[sizeof(ui.send.words_msg.buf)];
            {
                std::lock_guard lock(ui.send.words_msg.mtx);
                memcpy(buf, ui.send.words_msg.buf, sizeof(buf));
                ui.send.words_msg.buf[0] = '\0';
            }
            clientSend.words_msg_target = target;
            memcpy(clientSend.words_msg.buf, buf, sizeof(buf));
            //client.Send(target);
            //client.Send(buf, sizeof(buf));
            if (target != user::public_target) {
                std::lock_guard lock(mtx_users);
                auto& user = users[target];
                user.total_words_msg.buf.push_back(logger.name + ": " + buf);
            }
        }
        clientSend.share_video = share_video;
        clientSend.sc_w = sc_w;
        clientSend.sc_h = sc_h;
        clientSend.pk_size = pk_size;

        bool share_camera = get(ui.send.share_camera);
        std::vector<std::vector<uint8_t>> cam_packets(send_camera_packets_count);
        int32_t cam_pk_size = static_cast<int32_t>(send_camera_packets.pop(cam_packets.data(), send_camera_packets_count));

        clientSend.share_camera = share_camera;
        clientSend.cam_w = cam_w;
        clientSend.cam_h = cam_h;
        clientSend.cam_pk_size = cam_pk_size;

        s_MsgHton(clientSend);
        client.Send(&clientSend, sizeof(clientSend));

        {
            std::lock_guard lock{ ui.room_user.mtx_chosen_users };
            //client.Send<uint64_t>(ui.room_user.chosen_users.size());
            buf_chosen_users.clear();
            for (const auto& chosen_user : ui.room_user.chosen_users) {
                buf_chosen_users.push_back(chosen_user.first);
            }
            client.Send(buf_chosen_users.data(), sizeof(buf_chosen_users[0]) * buf_chosen_users.size());
            //client.Send(get(ui.room_user.chosen_user)); // 先send数量再send chosen_user
        }

        //client.Send(share_video);
        if (share_video) {
            //client.Send(sc_w);
            //client.Send(sc_h);
            //client.Send(pk_size);
            for (size_t i = 0;i < pk_size;++i) {
                auto& packet = packets[i];
                //client.Send(packet.data(), packet.size());
                client.Send(packet);
            }
        }

        //client.Send(share_camera);
        if (share_camera){
            //client.Send(cam_w);
            //client.Send(cam_h);
            //client.Send(cam_pk_size);
            for (int32_t i = 0; i < cam_pk_size; i++){
                client.Send(cam_packets[i]);
            }
        }
    }
}

void ClientApp::audioSend(){
    //DEFER{
    //	[this]() {p_show.wait_threads.release();}
    //};
    audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
    ad_cpt.Start();
    std::vector<float> buf_audio{};
    std::vector<uint8_t> audio_compressed{};

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
                audioCompressor.compress(audio_compressed, buf_audio);
                //client_audio.Send(buf_audio.data(), sizeof(buf_audio[0]) * buf_audio.size());
                client_audio.Send(audio_compressed.data(), sizeof(audio_compressed[0]) * audio_compressed.size());
            }
            //client_audio.Send(ui.room_user.is_muted_keep ? std::vector<float>{} : std::move(ad_cpt.giveFrames(buf_audio)));//move
        }
    }
}

void ClientApp::audioRecv(){
    DEFER{ [this]() {
        audioPlayer.stop();
        //p_show.wait_threads.release();
    }};
    audioPlayer.start();
    

    struct {
        user::id_t id{};
        faudio_t faudio{};
        std::vector<float> audioFrames{};
        std::vector<uint8_t> audio_compressed{};
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
            bool ok = client_audio.ReceiveBy(rv.audio_compressed, endSrcTar);
            if (ok) [[likely]] {
                if (rv.audio_compressed.empty()) {
                    rv.audioFrames.resize(0);
                }
                else {
                    audioCompressor.decompress(rv.audioFrames, rv.audio_compressed);
                }
            }
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
    //DEFER{
    //	[this]() {p_show.wait_threads.release();}
    //};
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
    ui.video.encode.wait_wh.release(2);
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

void ClientApp::CameraDecode() {
    //DEFER{ [this]() {
    //	p_show.wait_threads.release();
    //} };
    //H264Decoder decoder;
    //std::vector<H264Decoder> decoders;
    std::vector<std::shared_ptr<RecvDataEx>> buf_data;

    std::vector<std::vector<uint8_t>> pkts(recv_camera_packets_count);

    //auto last_time = std::chrono::steady_clock::now();
    while (!p_show.ended() && !clientClosed()) {
        std::this_thread::sleep_for(1ms);
        //const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        //	std::chrono::duration<boost::float64_t>(1.0 / get(ui.video.fps))
        //);
        //auto next_frame = last_time + target_interval;
        //std::this_thread::sleep_until(next_frame);
        //last_time = next_frame;

        {
            std::lock_guard lock{ ui.room_user.mtx_chosen_users };
            assert(buf_data.empty());
            for (const auto& ptr : ui.room_user.chosen_users) {
                buf_data.push_back(ptr.second);
            }
        }

        //decoders.resize(buf_data.size());
        for (size_t i = 0;i < buf_data.size();++i) {
            auto& data = buf_data[i];
            auto& recv_camera_packets = data->recv_camera_packets;
            auto& total_camera_frames = data->total_camera_frames;
            auto& decoder = data->cameraDecoder;
            //auto& decoder = decoders[i];
            const size_t pkts_count = recv_camera_packets.pop(pkts.data(), recv_camera_packets_count);
            for (size_t i = 0; i < pkts_count; ++i) {
                auto frames = decoder.DecodePacket(pkts[i]);
                for (auto& frame : frames) {
                    total_camera_frames.push(std::move(frame));
                }
            }
        }
        buf_data.clear(); // clear right after use to make sure destruct immediatly
    }
}

void ClientApp::ScreenDecode() {
    //DEFER{ [this]() {
    //	p_show.wait_threads.release();
    //} };
    //std::vector<H264Decoder> decoders;
    std::vector<std::shared_ptr<RecvDataEx>> buf_data;

    std::vector<std::vector<uint8_t>> pkts(recv_screen_packets_count);

    //auto last_time = std::chrono::steady_clock::now();
    while (!p_show.ended() && !clientClosed()) {
        std::this_thread::sleep_for(1ms);
        //const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        //	std::chrono::duration<boost::float64_t>(1.0 / get(ui.video.fps))
        //);
        //auto next_frame = last_time + target_interval;
        //std::this_thread::sleep_until(next_frame);
        //last_time = next_frame;


        {
            std::lock_guard lock{ ui.room_user.mtx_chosen_users };
            assert(buf_data.empty());
            for (const auto& ptr : ui.room_user.chosen_users) {
                buf_data.push_back(ptr.second);
            }
        }

        //decoders.resize(buf_data.size());
        for (size_t i = 0;i < buf_data.size();++i) {
            auto& data = buf_data[i];
            auto& recv_screen_packets = data->recv_screen_packets;
            auto& total_video_frames_v1 = data->total_video_frames_v1;
            auto& decoder = data->videoDecoder;
            //auto& decoder = decoders[i];

            const size_t pkts_count = recv_screen_packets.pop(pkts.data(), recv_screen_packets_count);
            for (size_t i = 0;i < pkts_count;++i) {
                auto frames = decoder.DecodePacket(pkts[i]);
                for (auto& frame : frames) {
                    auto frame_ptr = std::make_shared<ST::DecodedFrame>(std::move(frame));
                    if (getx(ui.room_user.record_video) && getx(ui.room_user.record_user) == data->user) {
                        if (mtx_videoRecord.try_lock_shared()) {
                            DEFER([this]() { mtx_videoRecord.unlock_shared(); });
                            if (videoRecordFrame[1 ^ videoRecordFrameRecording].images.empty()) {
                                videoRecordFrame[1 ^ videoRecordFrameRecording].images.push_back(frame_ptr);
                                videoRecordFrame[1 ^ videoRecordFrameRecording].videoPtsUs = static_cast<int64_t>(videoRecordTimer.microseconds());
                            }
                        }
                    }
                    total_video_frames_v1.push(frame_ptr);
                }
            }
        }
        buf_data.clear();
    }
}

void ClientApp::initVideoRecord() {
    videoRecordTimer.restart();
    recordAudioSamples = 0;
}

void ClientApp::videoRecord() {
    auto last_time = std::chrono::steady_clock::now();
    while (!get(ui.room_user.record_video));
    while (!p_show.ended() && !clientClosed() && get(ui.room_user.record_video)) {
        const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<boost::float64_t>(1.0 / videoRecorder.videoFps())
        );
        auto next_frame = last_time + target_interval;
        std::this_thread::sleep_until(next_frame);
        last_time = next_frame;

        {
            std::lock_guard lock(mtx_videoRecord);
            videoRecordFrameRecording ^= 1;
        }
        videoRecorder.addFramesTimed(
            videoRecordFrame[videoRecordFrameRecording].images,
            videoRecordFrame[videoRecordFrameRecording].videoPtsUs,
            videoRecordFrame[videoRecordFrameRecording].audio,
            videoRecordFrame[videoRecordFrameRecording].channels,
            videoRecordFrame[videoRecordFrameRecording].sampleRate,
            videoRecordFrame[videoRecordFrameRecording].audioPtsUs
        );
        videoRecordFrame[videoRecordFrameRecording].restore();
    }
    videoRecorder.save(::makeRecordPath("record/video", ".mp4"));
    println("end!!!");
}

static inline void audioDataStandard(std::vector<float>& buf, const size_t samples) {
    auto max_it =
        std::ranges::max_element(
            buf.begin(),
            buf.begin() + samples,
            [](float v1, float v2) {
                return std::abs(v1) <
                    std::abs(v2);
            }
        );


    if (max_it != buf.begin() + samples) {
        const float maxVal =
            std::abs(*max_it);

        if (maxVal > 1.0f) {
            std::ranges::for_each( buf.begin(), buf.begin() + samples, [maxVal](float& v) {
                if (!std::isfinite(v)) [[unlikely]] { v = 0; }
                v /= maxVal;
            });
        }
        else {
            std::ranges::for_each( buf.begin(), buf.begin() + samples, [](float& v) {
                if (!std::isfinite(v)) [[unlikely]] { v = 0; }
            });
        }
    }
}

void ClientApp::audioMix()
{
    //DEFER{
    //	[this]() { p_show.wait_threads.release(); }
    //};

    std::vector<float> mixBuf{};
    std::vector<float> mixBufWithOutMe{};
    std::vector<float> getBuf{};

    util::Timer timer;

    timer.restart();
    while (!p_show.ended() && !clientClosed()) {
        std::this_thread::sleep_for(50ms);



        const int sleep_ms = static_cast<int>(timer.milliseconds());
        timer.restart();
        const size_t samples = sleep_ms * audio::sampleRate / 1000 * audio::channels;
        mixBuf.resize(samples);
        mixBufWithOutMe.resize(samples);
        getBuf.resize(samples, 0);

        std::fill(mixBufWithOutMe.begin(), mixBufWithOutMe.end(), 0.0f );
        std::fill(mixBuf.begin(), mixBuf.end(), 0.0f);

        size_t realSamples = 0;

        {
            std::lock_guard lock{ mtx_users };
            // 每轮 pop 前先修剪
            //const size_t target_watermark = audio::sampleRate * audio::channels / 5;   // 200ms
            //std::pair < const user::id_t, User>* self_user{};
            for (auto& user : users) {
                const size_t thisSamples = cbPop(user.second.audioBuf, getBuf.data(), samples);
                //const size_t thisSamples = user.second.audioBuf.pop( getBuf.data(), samples );
                realSamples = std::max( realSamples, thisSamples );
                bool audio_playing{ false };
                if (user.first != logger.id) {
                    for (size_t i = 0; i < thisSamples; ++i) {
                        audio_playing |= std::abs(getBuf[i]) > User::audio_playing_least;
                        mixBufWithOutMe[i] += getBuf[i];
                        mixBuf[i] += getBuf[i];
                    }
                }
                // no self audio
                else {
                    for (size_t i = 0; i < thisSamples; ++i) {
                        audio_playing |= std::abs(getBuf[i]) > User::audio_playing_least;
                        mixBuf[i] += getBuf[i];
                    }
                }
                set(user.second.audio_playing, audio_playing);
            }
        }

        if (realSamples == 0) { continue; }

        audioDataStandard(mixBufWithOutMe, realSamples);
        audioDataStandard(mixBuf, realSamples);

        if (get(ui.room_user.record_video)) {
            if (mtx_videoRecord.try_lock_shared()) {
                DEFER([this]() { mtx_videoRecord.unlock_shared(); });
                auto& frame = videoRecordFrame[1 ^ videoRecordFrameRecording];
                const auto us = recordAudioSamples.fetch_add(static_cast<int64_t>(realSamples * 1000'000 / audio::sampleRate));
                if (frame.audio.empty()) {
                    frame.audioPtsUs = static_cast<int64_t>(us);
                }
                frame.audio.insert(frame.audio.end(), mixBuf.begin(), mixBuf.begin() + realSamples);
            }
        }

        audioUser.pushFrames(
#if LISTEN_MYSELF
            mixBuf.data(),
#else
            mixBufWithOutMe.data(),
#endif
            realSamples
        );

        {
            std::lock_guard lock(plug.asr.mtx);
            if (asrOn<false>()) {
                asrPushData<false>(mixBuf.data(), realSamples);
            }
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

void ClientApp::initBeforeShow() {
    auto first_id = get(logger.id);
    {
        std::lock_guard lock{ ui.room_user.mtx_chosen_users };
        ui.room_user.chosen_users.clear();
        ui.room_user.chosen_users.emplace(get(logger.id), std::make_shared<RecvDataEx>(getx(logger.id)));
    }
    //set(ui.room_user.chosen_user, get(logger.id)); // chosen_users push logger.id
    {
        std::lock_guard lock2{ mtx_users };
        auto [itr, first] = users.try_emplace(first_id);
        itr->second.name = logger.name;
    }
    

    videoRecordFrame[0].restore();
    videoRecordFrame[1].restore();
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
        client = net::tcp::Client{ net::tcp::Ip::Auto };
        client_audio = net::tcp::Client{ net::tcp::Ip::Auto };
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
        if (client.ConnectTo(net::tcp::Ip::Auto, p1.ipv4, p1.port) &&
            client.Send(connect_user) &&
            client.ReceiveBy(get_id) &&

            client_audio.ConnectTo(net::tcp::Ip::Auto, p1.ipv4, p1.port) &&
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


    loadUiState();
}

ClientApp::~ClientApp() {
    m_cameraCapture.Close();

    storeUiState();

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

template<bool mtx>
bool ClientApp::asrOn() {
    if constexpr (mtx) {
        std::lock_guard lock(plug.asr.mtx);
        return asrOn<false>();
    }
    else {
        return static_cast<bool>(plug.asr.close) && static_cast<bool>(plug.asr.open);
    }
}

void ClientApp::asrLoad() {
    std::lock_guard lk(plug.asr.mtx);

    if (plug.asr.stream) return; // 已经启用

    // 防止上一次异常残留未释放
    plug.asr.loader.free();

    auto clearup = [this]() {
        plug.asr.stream = nullptr;
        plug.asr.open = nullptr;
        plug.asr.push = nullptr;
        plug.asr.poll = nullptr;
        plug.asr.finish = nullptr;
        plug.asr.reset = nullptr;
        plug.asr.close = nullptr;
    };

    // 1. 加载 DLL
    if (!plug.asr.loader.load((plug.asr.dir +  "/plugin_ASR.dll").c_str())) {
        std::lock_guard ui_lk(plug.asr.ui.mtx);
        plug.asr.ui.status = "LoadLibrary failed, code: " + std::to_string(GetLastError());
        return;
    }

    // 2. 获取函数指针
    bool got_funcs =
        plug.asr.loader.get(plug.asr.open, plugin::asr::name::asrOpen) &&
        plug.asr.loader.get(plug.asr.push, plugin::asr::name::asrPush) &&
        plug.asr.loader.get(plug.asr.poll, plugin::asr::name::asrPoll) &&
        plug.asr.loader.get(plug.asr.finish, plugin::asr::name::asrFinish) &&
        plug.asr.loader.get(plug.asr.reset, plugin::asr::name::asrReset) &&
        plug.asr.loader.get(plug.asr.close, plugin::asr::name::asrClose);

    if (!got_funcs) {
        clearup();
        plug.asr.loader.free();

        std::lock_guard ui_lk(plug.asr.ui.mtx);
        plug.asr.ui.status = "ASR Load Failed: Missing functions";
        return;
    }

    // 3. 打开 ASR 流
    char error[512] = {};
    int32_t rc = plug.asr.open((plug.asr.dir + "/model").c_str(), 0, &plug.asr.stream, error, sizeof(error));
    if (rc != PLUGIN_ASR_OK || !plug.asr.stream) {
        clearup();
        plug.asr.loader.free(); // 失败必须卸载
        std::lock_guard ui_lk(plug.asr.ui.mtx);
        plug.asr.ui.status = "ASR open failed: " + std::string(error);
        return;
    }

    // 4. 启用成功
    std::lock_guard ui_lk(plug.asr.ui.mtx);
    plug.asr.ui.status = "ASR Started";
}

void ClientApp::asrDownload() {
    std::lock_guard lk(plug.asr.mtx);
    if (!plug.asr.stream && !plug.asr.close) return;

    // 1. 提取指针并立刻将全局状态置空
    auto* stream_to_close = plug.asr.stream;
    auto close_fn = plug.asr.close;

    plug.asr.stream = nullptr;
    plug.asr.open = nullptr;
    plug.asr.push = nullptr;
    plug.asr.poll = nullptr;
    plug.asr.finish = nullptr;
    plug.asr.reset = nullptr;
    plug.asr.close = nullptr;

    // 2. 直接强杀对象（底层 abort_callback 会瞬间打断推理并释放内存）
    if (stream_to_close) {
        try {
            if (close_fn) {
                close_fn(stream_to_close);
            }
        }
        catch (...) {}
    }

    // 3. 彻底卸载 DLL（绝对不泄漏）
    plug.asr.loader.free();

    // 4. 清理 UI
    std::lock_guard ui_lk(plug.asr.ui.mtx);
    plug.asr.ui.history = {};
    plug.asr.ui.partial = {};
    plug.asr.ui.partialStay = {};
    plug.asr.ui.status = "ASR Stopped";
}

template<bool mtx>
void ClientApp::asrPushData(const float* audioFrames, size_t count) {
    static_assert(!mtx); // on purpose
    //std::lock_guard lk(plug.asr.mtx);
    if (plug.asr.stream && plug.asr.push) {
        int32_t r = plug.asr.push(plug.asr.stream, audioFrames, static_cast<uint32_t>(count));

        if (r == PLUGIN_ASR_INVALID) {
            std::lock_guard ui_lk(plug.asr.ui.mtx);
            plug.asr.ui.status = "Push INVALID (audio data rejected)";
        }
        //else {
        //	std::lock_guard ui_lk(plug.asr.ui.mtx);
        //	plug.asr.ui.status.clear();
        //}
    }
}

std::string ClientApp::asrHistoryPath() {
    std::filesystem::path path{ plug.asr.dir };
    path = path / "record" / std::to_string(logger.room_id) / ".txt";
    return path.generic_string();
}

std::string ClientApp::asrRecentHistory() {
    return plug.asr.ui.history;
}

std::string ClientApp::asrTotalHistory() {
    return ::read_all_file(asrHistoryPath()) + plug.asr.ui.history;
}

template<bool mtx>
bool ClientApp::translatorOn(){
    if constexpr (mtx) {
        std::lock_guard lock{ plug.translator.mtx };
        return translatorOn<false>();
    }
    else {
        return static_cast<bool>(plug.translator.Registor) && static_cast<bool>(plug.translator.close);
    }
}

void ClientApp::translatorLoad(){
    std::lock_guard lock{ plug.translator.mtx };
    if (translatorOn<false>()) { return; }
    bool ok = plug.translator.loader.load((plug.translator.dir + "/plugin_Translator.dll").c_str());

    auto clearup = [this]() {
        plug.translator.close = nullptr;
        plug.translator.translate = nullptr;
        plug.translator.unload = nullptr;
        plug.translator.Registor = nullptr;
        plug.translator.loader.free();
    };

    if (!ok) {
        {
            std::lock_guard lock{ plug.translator.mtx_ui };
            plug.translator.error = "load error";

            std::cerr << "LoadLibrary failed, GetLastError = " << GetLastError() << "\n";
            std::cerr << "  path = " << std::filesystem::absolute(plug.translator.dir + "/plugin_Translator.dll") << "\n";
        }
        clearup();
        return;
    }
    ok =
    plug.translator.loader.get(plug.translator.Registor, plugin::translator::name::Register) &&
    plug.translator.loader.get(plug.translator.unload, plugin::translator::name::unload) &&
    plug.translator.loader.get(plug.translator.translate, plugin::translator::name::translate) && 
    plug.translator.loader.get(plug.translator.close, plugin::translator::name::close);
    if (!ok) {
        {
            std::lock_guard lock{ plug.translator.mtx_ui };
            plug.translator.error = "get func error";
        }
        clearup();
        return;
    }
    plug.translator.error.clear();

    translatorLoadDirections();
    if (!plug.translator.directions.empty()) {
        plug.translator.direction = plug.translator.directions.front();
    }
    for (const auto& d : plug.translator.directions) {
        char* error{};
        if(!plug.translator.Registor(d.src.c_str(), d.tar.c_str(), d.model_dir.c_str(), &error)) {
            translatorSetError<true>(error);
        }
    }
}

void ClientApp::translatorDownload() {
    std::lock_guard lock{ plug.translator.mtx }; // 如果卡在这，说明后台没释放
    if (plug.translator.unload) {
        char* error{};
        plug.translator.unload(plug.translator.direction.src.c_str(), plug.translator.direction.tar.c_str(), &error);
    }
    if (plug.translator.close) {
        plug.translator.close();
    }
    plug.translator.close = nullptr;
    plug.translator.translate = nullptr;
    plug.translator.unload = nullptr;
    plug.translator.Registor = nullptr;
    plug.translator.loader.free(); // 如果卡在这，说明 DLL 卸载卡住了
    plug.translator.directions = {};
}

void ClientApp::translatorLoadDirections() {
    plug.translator.directions.clear();
    if (plug.translator.dir.empty()) {
        return;
    }

    // <plug.translator.dir>/models/tables.ini
    std::filesystem::path modelsDir = std::filesystem::path(plug.translator.dir) / "models";
    std::filesystem::path tablesPath = modelsDir / "tables.ini";

    std::ifstream file(tablesPath);
    if (!file.is_open()) {
        translatorSetError<true>("Failed to open " + tablesPath.string());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        TranslateDirection dir;
        if (iss >> dir.src >> dir.tar >> dir.model_dir) {
            dir.model_dir = (modelsDir / dir.model_dir).generic_string();
            plug.translator.directions.push_back(std::move(dir));
        }
    }
}

void ClientApp::translateThread() {
    char* out{};
    char* error{};
    Record<std::string> toBeTranslated{ "" };
    TranslateDirection direction;
    while (!p_show.ended() && !clientClosed() && translatorOn<true>()) {
        std::this_thread::sleep_for(50ms);
        std::lock_guard lock{ plug.translator.mtx };
        {
            std::lock_guard lock{ plug.translator.mtx_ui };
            std::lock_guard lock_asr{ plug.asr.ui.mtx };
            plug.translator.toBeTranslated = plug.asr.ui.partialStay;
            toBeTranslated.update(plug.translator.toBeTranslated);
        }
        if (plug.translator.translate && toBeTranslated.changed()) {
            {
                std::lock_guard lock{ plug.translator.mtx_ui };
                direction = plug.translator.direction;
            }
            if (!plug.translator.translate(toBeTranslated.now.c_str(), direction.src.c_str(), direction.tar.c_str(), &out, &error)) {
                translatorSetError<true>(error);
            }
            else {
                std::lock_guard lock{ plug.translator.mtx_ui };
                plug.translator.result = out;
            }
        }
        else if (!plug.translator.translate) {
            translatorSetError<true>("plug.translator.translate is nullptr");
        }
    }

    println("translate thread end");
}

template<bool mtx>
bool ClientApp::summaryOn(){
    auto st = plug.summary;   // 拷贝一份 shared_ptr：本函数返回前实例不会被释放
    if (!st) { return false; }
    if constexpr (mtx) {
        std::lock_guard lock{ st->mtx };
    }
    return static_cast<bool>(st->start) && static_cast<bool>(st->stop);
}

template<bool mtx>
bool ClientApp::summaryReady() {
    auto st = plug.summary;
    if (!st) { return false; }
    if constexpr (mtx) {
        std::lock_guard lk{ st->mtx };
    }
    return st->ready() != 0;
}

void ClientApp::summaryLoad(){
    if (summaryOn<false>()) { return; }

    // 上一个实例可能还在后台收尾（释放模型/DLL）：等它结束再加载，
    // 否则两个实例会同时操作 DLL 里的全局 AiLauncher。
    // download 已经发过 cancel，这一步通常只需要等很短时间。
    {
        std::unique_lock lk{ g_summary_teardown_mtx };
        g_summary_teardown_cv.wait(lk, [] { return g_summary_teardown == 0; });
    }

    auto st = std::make_shared<SummaryPlugin>();
    st->dir = plug.summary_dir;
    plug.summary_error.clear();

    if (!st->loader.load((st->dir + "/plugin_Summary.dll").c_str())) {
        plug.summary_error = "LoadLibrary failed, code: " + std::to_string(GetLastError());
        std::cout << "[summary] " << plug.summary_error << "\n";
        return;
    }

    bool ok =
        st->loader.get(st->start, plugin::summary::name::start) &&
        st->loader.get(st->ready, plugin::summary::name::ready) &&
        st->loader.get(st->cancel, plugin::summary::name::cancel) &&
        st->loader.get(st->generate, plugin::summary::name::generate) &&
        st->loader.get(st->lasterror, plugin::summary::name::lasterror) &&
        st->loader.get(st->stop, plugin::summary::name::stop);
    if (!ok) {
        plug.summary_error = "get func error";
        std::cout << "[summary] " << plug.summary_error << "\n";
        return;   // st 析构时 Loader 会 FreeLibrary
    }

    st->running = true;
    plug.summary = st;
    st->thread = std::jthread{ [st] { ClientApp::summaryThread(st); } };

    std::lock_guard lk{ st->ui.mtx };
    st->ui.error.clear();
}

void ClientApp::summaryDownload(){
    // 两条调用路径都走这里：
    //   1) 设置面板里取消勾选 load —— 普通卸载；
    //   2) Show() 退出（关窗口 / 被踢 / 断线）—— PageShow.inl 末尾会调一次。
    // 这里把实例从 UI 手里摘走后就返回，绝不等 generate：收尾全部交给后台线程。
    auto st = std::move(plug.summary);
    if (!st) { return; }

    // 1. 通知工作线程退出，并打断进行中的 generate（cancel 会打断推理）。
    st->running = false;
    if (st->cancel) { st->cancel(); }
    st->busy.release();

    // 2. 后台收尾：join 工作线程（它内部会 join 生成线程）→ stop 引擎 → 释放 DLL。
    //    st 由收尾线程和 summaryThread 共同持有，最后一个用完才析构，
    //    所以不需要 timeout、detach、快照这些补丁。
    {
        std::lock_guard lk{ g_summary_teardown_mtx };
        ++g_summary_teardown;
    }
    std::thread([st = std::move(st)]() mutable {
        if (st->thread.joinable()) { st->thread.join(); }
        if (st->stop) { st->stop(); }
        st->loader.free();
        {
            std::lock_guard lk{ g_summary_teardown_mtx };
            --g_summary_teardown;
        }
        g_summary_teardown_cv.notify_all();
    }).detach();
}

// 只保留末尾 maxBytes 字节，并从整行开始，避免切断多字节字符或半行。
static std::string summary_tail(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) { return s; }
    size_t begin = s.size() - maxBytes;
    while (begin < s.size() && (static_cast<unsigned char>(s[begin]) & 0xC0) == 0x80) { ++begin; }
    const size_t nl = s.find('\n', begin);
    if (nl != std::string::npos && nl + 1 < s.size()) { begin = nl + 1; }
    return s.substr(begin);
}

static long long summary_now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void ClientApp::summaryThread(std::shared_ptr<SummaryPlugin> st) {
    std::string prompt;
    std::string content;
    std::string src;
    std::string model;
    std::jthread worker;
    for (;;) {
        st->busy.acquire();
        if (!st->running.load()) { break; }
        {
            std::lock_guard lk_ui{ st->ui.mtx };
            st->ui.error.clear();
            content = st->ui.content;
            prompt = st->ui.prompt;
            src = st->ui.src;
            model = st->ui.modelName;
        }
        if (!st->start || !st->stop) { break; }   // 正在卸载

        // 加载模型比较慢，但不持 UI 的锁：UI 只是读 st->ready()。
        if (!st->ready()) {
            int32_t ok = st->start(src.c_str(), model.c_str());
            std::lock_guard lk_ui{ st->ui.mtx };
            if (!ok) {
                st->ui.error = st->lasterror();
                std::cout << "error here: " << st->ui.error << "\n";
            }
            else {
                st->ui.modelLoaded = true;
            }
            continue;
        }
        if (content.empty()) {
            std::lock_guard lk_ui{ st->ui.mtx };
            st->ui.result.clear();
            st->ui.error = (char*)u8"没有可总结的文本：先加载 asr 并展开 asr 面板让它出字";
            continue;
        }
        if (st->generating.load()) {
            continue;   // 上一次还在跑；界面上有进度提示，不再覆盖成错误
        }

        // 3B 模型纯 CPU 推理，提示词越长越慢；只取最近一段。
        const size_t raw_bytes = content.size();
        content = summary_tail(content, 1500);

        st->generating = true;
        st->cancel_sent = false;
        st->gen_start_ms = summary_now_ms();
        worker = std::jthread{ [st, content, prompt, raw_bytes]() {
            using clock = std::chrono::steady_clock;
            const auto t0 = clock::now();
            std::cout << "[summary] generate begin: " << raw_bytes << " -> " << content.size() << " bytes\n";

            // st 由本线程持有，收尾线程会先 join 本线程再释放 DLL，
            // 所以调用期间 DLL 一定还在，不需要保存函数指针副本。
            const char* res = st->generate
                ? st->generate(prompt.c_str(), content.c_str())
                : nullptr;

            std::string result{};
            if (res) { result = res; }
            {
                std::lock_guard lk_ui{ st->ui.mtx };
                if (res) {
                    st->ui.result = result;
                }
                else {
                    st->ui.result.clear();
                    st->ui.error = st->lasterror();
                }
            }
            st->generating = false;
            std::cout << "[summary] generate end: took "
                << std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count()
                << " ms, result " << result.size() << " bytes\n";
        } };
    }
    // 退出段：cancel 让生成线程尽快返回，再 join。
    // 不在这里 stop/释放 DLL：由 summaryDownload 的后台收尾线程在本线程退出后完成。
    if (st->cancel) { st->cancel(); }
    if (worker.joinable()) { worker.join(); }
    st->generating = false;
}

#include <PageShow.inl>

int main() {
    auto app = std::make_unique<ClientApp>();
    app->run();
}