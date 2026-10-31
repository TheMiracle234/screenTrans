#include <ClientApp.hpp>
#include <imgui_ex.hpp>
#include <filesystem>
#include <wins/wc_u8.hpp>

namespace fs = std::filesystem;

inline ClientApp::Page ClientApp::Show() {
	initBeforeShow();

	constexpr size_t thread_count = 10;

	DEFER{ [this, thread_count]() {
		//client.Close();
		//client_audio.Close();
		//for (size_t i = 0;i < thread_count;++i) {
			//p_show.wait_threads.acquire();
		//}
		restoreAfterShow();
	} };
	{

		std::jthread threads[] = {
			std::jthread{ &ClientApp::audioMix, this },
			std::jthread{ &ClientApp::Send, this },
			std::jthread{ &ClientApp::Receive, this },
			std::jthread{ &ClientApp::audioSend, this },
			std::jthread{ &ClientApp::audioRecv, this },
			std::jthread{ &ClientApp::ScreenCaptureEncode, this },
			std::jthread{ &ClientApp::ScreenDecode, this },
			std::jthread{ &ClientApp::CameraCaptureThread, this },
			std::jthread{ &ClientApp::CameraCaptureEncode, this },
			std::jthread{ &ClientApp::CameraDecode, this },
		};

		static_assert(thread_count == sizeof(threads) / sizeof(std::jthread));

		ui.video.encode.wait_wh.acquire();
		const int sc_width{ ui.video.encode.width };
		const int sc_height{ ui.video.encode.height };

		ui.camera.encode.wait_wh.acquire();
		const int cam_width{ ui.camera.encode.width };
		const int cam_height{ ui.camera.encode.height };

		//assert(thread_count == threads.size());

		glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());

		struct imgStruct {
			float pos[3];
			float texCoords[2];
		};

		gl::Texture2D img(1, 1, { 0x33,0x33,0x33,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
		gl::Texture2D camTexture(1, 1, { 0,0,0,0 }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
		std::vector<uint8_t> camPixelBuffer;
		ST::DecodedFrame cam_frame;

		std::unique_ptr<std::jthread> record_thread;


#if USE_IMGUI
		const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif


		boost::float64_t d_time = 0;
		float fps = 0;
		bool show_settings = true;
		struct {
			char words_msg_buf[sizeof(ui.send.words_msg.buf)]{};
			char name_buf[2 * sizeof(p1.name)]{};
			char rename[sizeof(p1.name)]{};
			uint32_t room_passwd{ Room::invalid_passwd };
		} mid;
		std::shared_ptr<ST::DecodedFrame> decodedFrame;


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

			DEFER{
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
					if (ui.send.words_msg_target == user::public_target) {
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
				if (ImGui::TreeNode("screen_video")) {
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
				}// screen_video
				if (ImGui::TreeNode("camera_video")) {
					ImGui::PushIdGuard pushIdGuard(__LINE__);
					// fps
					ImGui::Text("FPS: %.3f", fps);
					if (ImGui::TreeNode("encode")) {
						// fps
						COMP_ATOMIC(ImGui::SliderInt, "fps", ui.camera.encode.fps, static_cast<int>(MIN_MAX_FPS), static_cast<int>(MAX_MAX_FPS));
						// bitrate
						COMP_ATOMIC(ImGui::SliderInt, "bitrate", ui.camera.encode.bitrate, video::min_bitrate, video::max_bitrate);
						// width
						COMP_ATOMIC_EX(ImGui::SliderInt, "width", ui.camera.encode.width, [](int v) {return v & ~1;}, std::max(static_cast<int>(cam_width * 0.1), 1), cam_width);
						// height
						COMP_ATOMIC_EX(ImGui::SliderInt, "height", ui.camera.encode.height, [](int v) {return v & ~1;}, std::max(static_cast<int>(cam_height * 0.1), 1), cam_height);
						ImGui::TreePop();
					}// encode
					ImGui::TreePop();
				}// screen_video
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
					// record_video
					bool record_video0 = get(ui.room_user.record_video);
					bool record_video1 = record_video0;
					ImGui::Checkbox("record_video", &record_video1);
					if (record_video0 != record_video1 && record_video1) {
						initVideoRecord();
						record_thread = std::make_unique<std::jthread>(&ClientApp::videoRecord, this);
					}
					set(ui.room_user.record_video, record_video1);

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
					if (ImGui::TreeNode("asr")) {
						ImGui::PushIdGuard pushIdGuard(__LINE__);
						bool on0 = asrOn<false>();
						bool on1 = on0;
						if (ImGui::BeginCombo("plugins", plug.asr.dir.c_str())) {
							if (!on0) {
								std::string dir;
								for (const auto& entry : fs::directory_iterator("plugin/asr")) {
									wins::WideToUtf8(dir, entry.path().c_str());
									if (ImGui::Selectable(dir.c_str())) {
										plug.asr.dir = std::move(dir);
										break;
									}
								}
							}
							ImGui::EndCombo();
						}
						if (!plug.asr.dir.empty()) {
							ImGui::Checkbox("load", &on1);
						}
						if (on0 != on1) {
							if (on1) {
								asrLoad();
							}
							else {
								asrDownload();
							}
						}
						if (on1) {
							ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
							ImGui::PushTextWrapPos(0.0f);

							{
								std::lock_guard lk(plug.asr.mtx);
								if (plug.asr.stream && plug.asr.poll) {
									plugin::asr::drain_events(plug.asr.stream, plug.asr.poll, plug.asr.ui);
								}
								std::lock_guard ui_lk(plug.asr.ui.mtx);
								if (!plug.asr.ui.history.empty()) {
									ImGui::TextUnformatted(plug.asr.ui.history.c_str());
								}
								if (!plug.asr.ui.partial.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
									ImGui::TextUnformatted(plug.asr.ui.partial.c_str());
									ImGui::PopStyleColor();
								}
								if (!plug.asr.ui.status.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
									ImGui::TextUnformatted(plug.asr.ui.status.c_str());
									ImGui::PopStyleColor();
								}
							}
							ImGui::PopTextWrapPos();
							ImGui::EndChild();
						}
						ImGui::TreePop();
					}// asr
					ImGui::TreePop();
				}// plugin

				ImGui::End();
			}// settings


				// ============ main window ============ 
			style->Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
			ImGui::Begin("screen", nullptr, ImGuiWindowFlags_NoScrollbar);
			{
				const float titleBarH = ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoTitleBar ? 0.0f : ImGui::GetFrameHeight();
				ImVec2 winSize = ImGui::GetWindowSize();
				winSize.y -= titleBarH;
				//const auto imgSize = img.imgSize();
				ImVec2 show_img_size;

				float offsetX{};
				float offsetY{};
				const float img_w_y = static_cast<float>(rv_sc_orig_w) / rv_sc_orig_h;
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
				offsetY += titleBarH;
				ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
				ImGui::Image(img.id(), show_img_size);
			}
			ImGui::End();// main

			//////////////////////////// camera
			const user::id_t target_id = get(ui.room_user.chosen_user);
			bool target_share_camera;
			{
				std::lock_guard lock(mtx_users);
				target_share_camera = users[target_id].share_camera;
			}

			if (!total_camera_frames.empty()) {
				total_camera_frames.pop(cam_frame);
			}

			int cam_w = cam_frame.width;
			int cam_h = cam_frame.height;
			auto& cam_data = cam_frame.rgba;
			if (cam_w > 0 && cam_h > 0 && cam_data.size() == cam_w * cam_h * 4) {
				camTexture.bindTo(0);
				camTexture.resize(nullptr, cam_w, cam_h, 4);
				camTexture.resetData(cam_frame.rgba, 4);
			}


			if (target_share_camera) {
				ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowPos(ImVec2(static_cast<float>(sc_width - 340), 20.0f), ImGuiCond_FirstUseEver);
				ImGui::Begin("camera", nullptr, ImGuiWindowFlags_NoScrollbar);

				const float titleBarH = ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoTitleBar ? 0.0f : ImGui::GetFrameHeight();
				ImVec2 winSize = ImGui::GetWindowSize();
				winSize.y -= titleBarH;
				const float img_w_y = static_cast<float>(rv_cam_orig_w) / rv_cam_orig_h;
				const float win_w_y = winSize.x / winSize.y;
				ImVec2 show_img_size;
				float offsetX, offsetY;
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
				offsetY += titleBarH;
				ImGui::SetCursorPos(ImVec2{ offsetX, offsetY });
				ImGui::Image(camTexture.id(), show_img_size, ImVec2(0, 1), ImVec2(1, 0));
				ImGui::End();
			}
			////////////////////////////



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


			int width{};
			int height{};
			std::vector<uint8_t>* data{};

			if (decodedFrame) [[likely]] {
				width = decodedFrame->width;
				height = decodedFrame->height;
				data = &decodedFrame->rgba;
			}


			if (decodedFrame && width > 0 && height > 0 && data->size() == (size_t)width * height * 4) {
				bool share_video;
				{
					std::lock_guard lock(mtx_users);
					share_video = users[get(ui.room_user.chosen_user)].share_video;
				}

				img.bindTo(0);

				if (share_video) {
					img.resize(nullptr, width, height, 4);
					img.resetData(*data, 4);
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

		client.Close();
		client_audio.Close();
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