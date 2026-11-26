#include <ClientApp.hpp>
#include <imgui_ex.hpp>
#include <filesystem>
#include <wins/wc_u8.hpp>
#include <format>
#include <chrono>

namespace fs = std::filesystem;

template <class T, class... Args>
void resize_emplace(std::vector<T>& v, std::size_t n, Args&&... args) {
	const std::size_t old = v.size();
	if (n <= old) {
		v.erase(v.begin() + n, v.end());   // ✅ 只析构，不需要默认构造
		return;
	}
	v.reserve(n);
	for (std::size_t i = old; i < n; ++i) {
		v.emplace_back(std::forward<Args>(args)...);
	}
}

// 从 [0, targetSplitPos) 范围内，从 targetSplitPos 往回找第一个 '\n'
// 找到：savedPart = s[0..pos+1]，返回 s[pos+1..end]
// 找不到换行：直接返回整个字符串，savedPart 为空（不分割）
[[nodiscard]]
static std::string_view split_at_newline_near(std::string_view s, size_t targetSplitPos, std::string& savedPart) noexcept
{
	savedPart.clear();
	if (s.empty()) return s;

	// 防止越界
	targetSplitPos = std::min(targetSplitPos, s.size());
	if (targetSplitPos == 0) return s;

	const char* const begin = s.data();
	const char* const p = begin + targetSplitPos;

	// 反向查找 '\n'：从 p-1 往 begin 扫
	const char* found = nullptr;
	for (const char* it = p - 1; it >= begin; --it)
	{
		if (*it == '\n')
		{
			found = it;
			break;
		}
	}

	if (!found)
	{
		// 这段里没有换行，不切割
		return s;
	}

	const size_t splitIdx = found - begin + 1; // 包含这个 \n
	savedPart.assign(begin, splitIdx);
	return s.substr(splitIdx);
}


struct loadPluginRes {
	bool on;
	bool changed;
};

static inline loadPluginRes loadPlugin(const char* plug_type_dir, std::string& plug_dir, ClientApp* app, bool on, void (ClientApp::*load)(), void (ClientApp::* download)()) {
	const bool on0 = on;
	bool on1 = on0;
	loadPluginRes res;
	if (!on0) {
		ImGui::SelectFile("plugins", plug_type_dir, plug_dir);
	}
	else if (ImGui::BeginCombo("plugins", plug_dir.c_str())) {
		ImGui::EndCombo();
	}
	//if (ImGui::BeginCombo("plugins", plug_dir.c_str())) {
	//	if (!on0) {
	//		std::string dir;
	//		for (const auto& entry : fs::directory_iterator(plug_type_dir)) {
	//			dir = entry.path().generic_string().c_str();
	//			if (ImGui::Selectable(dir.c_str())) {
	//				plug_dir = std::move(dir);
	//				break;
	//			}
	//		}
	//	}
	//	ImGui::EndCombo();
	//}
	if (!plug_dir.empty()) {
		ImGui::Checkbox("load", &on1);
	}
	if (on0 != on1) {
		res.changed = true;
		if (on1) {
			(*app.*load)();
		}
		else {
			(*app.*download)();
		}
	}
	else {
		res.changed = false;
	}
	res.on = on1;
	return res;
}

inline ClientApp::Page ClientApp::Show() {
	initBeforeShow();

	//constexpr size_t thread_count = 10;

	DEFER{ [this]() {
		//client.Close();
		//client_audio.Close();
		//for (size_t i = 0;i < thread_count;++i) {
			//p_show.wait_threads.acquire();
		//}
		restoreAfterShow();
	} };

	bool windowClosed = false;
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

		//static_assert(thread_count == sizeof(threads) / sizeof(std::jthread));

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

		std::vector<gl::Texture2D> imgs;
		////gl::Texture2D img(1, 1, { 0x33,0x33,0x33,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
		std::vector<gl::Texture2D> camTextures;
		////gl::Texture2D camTexture(1, 1, { 0,0,0,0 }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
		//std::vector<uint8_t> camPixelBuffer;
		//ST::DecodedFrame cam_frame;

		std::unique_ptr<std::jthread> threadVideoRecord;
		std::unique_ptr<std::jthread> threadTranslate;

#if USE_IMGUI
		const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif

		bool save_words_message{};
		std::string words_message_tobesaved;

		boost::float64_t d_time = 0;
		float fps = 0;
		bool show_settings = true;
		bool at_bottom = true;
		int max_history_bytes = 2048;
		struct {
			char words_msg_buf[sizeof(ui.send.words_msg.buf)]{};
			char name_buf[2 * sizeof(p1.name)]{};
			char rename[sizeof(p1.name)]{};
			uint32_t room_passwd{ Room::invalid_passwd };
			std::string summary_model{};
		} mid;
		//std::shared_ptr<ST::DecodedFrame> decodedFrame;


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
						if (!save_words_message) {
							for (const auto& msg : ui.send.total_words_msg.buf) {
								ImGui::TextUnformatted(msg.c_str());
							}
						}
						else {
							for (const auto& msg : ui.send.total_words_msg.buf) {
								ImGui::TextUnformatted(msg.c_str());
								words_message_tobesaved += (msg + "\n");
							}
							::saveFile(::makeRecordPath("record/words_message", ".txt"), words_message_tobesaved.data(), words_message_tobesaved.size());
							words_message_tobesaved.clear();
						}
					}
					else {
						std::lock_guard lock(mtx_users);
						auto& msgs = users[get(ui.send.words_msg_target)].total_words_msg;
						if (!save_words_message) {
							for (const auto& msg : msgs.buf) {
								ImGui::TextUnformatted(msg.c_str());
							}
						}
						else {
							for (const auto& msg : msgs.buf) {
								ImGui::TextUnformatted(msg.c_str());
								words_message_tobesaved += (msg + "\n");
							}
							::saveFile(::makeRecordPath("record/words_message", ".txt"), words_message_tobesaved.data(), words_message_tobesaved.size());
							words_message_tobesaved.clear();
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
					save_words_message = ImGui::Button("save");

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

					// record_user
					std::string current_record_name;
					user::id_t record_uid = get(ui.room_user.record_user);
					{
						std::lock_guard lock(mtx_users);
						auto it = users.find(record_uid);
						if (it != users.end()) {
							current_record_name = it->second.name;
						}
					}
					ImGui::Text("Record User:");
					if (ImGui::BeginCombo("##record_user_combo", current_record_name.c_str())) {
						if (!get(ui.room_user.record_video)) {
							std::lock_guard lock(mtx_users);
							for (const auto& user : users) {
								ImGui::PushU64Guard pushU64Guard(user.first);
								bool is_selected = (record_uid == user.first);
								if (ImGui::Selectable(user.second.name.c_str(), is_selected)) {
									set(ui.room_user.record_user, user.first);
								}
								if (is_selected) {
									ImGui::SetItemDefaultFocus();
								}
							}
						}
						ImGui::EndCombo();
					}

					// record_video
					bool record_video0 = get(ui.room_user.record_video);
					bool record_video1 = record_video0;
					if (get(ui.room_user.record_user) != user::invalid_id) {
						ImGui::Checkbox("record_video", &record_video1);
						if (record_video0 != record_video1 && record_video1) {
							initVideoRecord();
							threadVideoRecord = std::make_unique<std::jthread>(&ClientApp::videoRecord, this);
						}
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
						std::lock_guard lock0(ui.room_user.mtx_chosen_users);
						std::lock_guard lock1(mtx_users);
						//auto chosen = users.find(get(ui.room_user.chosen_user));
						//assert(chosen != users.end());
						////auto name_chosen = users[chosen->first].name + (get(users[chosen->first].is_host) ? "(host)" : "");
						//auto& chosen_user = users[chosen->first];
						constexpr char host_signal[] = "(host)";
						//memcpy(mid.name_buf, chosen_user.name.c_str(), chosen_user.name.size() + 1);
						//if (get(chosen_user.is_host)) {
						//	memcpy(mid.name_buf + chosen_user.name.size(), &host_signal, sizeof(host_signal));
						//}
						//ImGui::Text("target:");
						//if (ImGui::TreeNode("target", mid.name_buf)) { // changed to "target" directly
						if (ImGui::TreeNode("target", "choose_target")) { // changed to "target" directly
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
									bool chosen = ui.room_user.chosen_users.count(user.first);
									ImGui::Checkbox(user.second.name.c_str(), &chosen);
									if (chosen) {
										ui.room_user.chosen_users.emplace(user.first, std::make_shared<RecvDataEx>(user.first));
									}
									else {
										ui.room_user.chosen_users.erase(user.first);
									}
									//if (ImGui::Selectable(mid.name_buf)) {
									//	std::lock_guard lock{ ui.room_user.mtx_chosen_users };
									//	ui.room_user.chosen_users.emplace(user.first);
									//	//set(ui.room_user.chosen_user, user.first); // try_emplace
									//}
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
					ImGui::Text("plugin allow: ");
					COMP_ATOMIC(ImGui::Checkbox, "allow_asr", ui.room_user.host.allow_asr);
					COMP_ATOMIC(ImGui::Checkbox, "allow_translator", ui.room_user.host.allow_translator);
					COMP_ATOMIC(ImGui::Checkbox, "allow_summary", ui.room_user.host.allow_summary);
					ImGui::TreePop();
				}// host
				if (ImGui::TreeNode("plugin")) {
					while (ImGui::TreeNode("asr")) {
						DEFER{ ImGui::TreePop };

						if (!get(ui.plugin.allow_asr)) {
							ImGui::Text("disabled by host");
							break;
						}
						ImGui::PushIdGuard pushIdGuard(__LINE__);
						auto [on, changed]= ::loadPlugin("plugin/asr", plug.asr.dir, this, asrOn<false>(), &ClientApp::asrLoad, &ClientApp::asrDownload);
						if (on) {
							ImGui::BeginChild("##asr", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
							ImGui::PushTextWrapPos(0.0f);

							std::string partialStay;
							{
								std::lock_guard lk(plug.asr.mtx);
								if (plug.asr.stream && plug.asr.poll) {
									plugin::asr::drain_events(plug.asr.stream, plug.asr.poll, plug.asr.ui); // locked inside
								}
								std::lock_guard ui_lk(plug.asr.ui.mtx);
								if (!plug.asr.ui.history.empty()) {
									if (plug.asr.ui.history.size() > max_history_bytes) [[unlikely]] {
										std::string save;
										std::string_view remain = ::split_at_newline_near(plug.asr.ui.history, max_history_bytes / 2, save);
										::file_append(asrHistoryPath(), save.data(), sizeof(save));
										plug.asr.ui.history = remain;
									}
									ImGui::TextUnformatted(plug.asr.ui.history.c_str());
								}

								if (!plug.asr.ui.partialStay.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
									ImGui::TextUnformatted(plug.asr.ui.partialStay.c_str());
									partialStay = plug.asr.ui.partialStay;
									ImGui::PopStyleColor();
								}
								if (!plug.asr.ui.status.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
									ImGui::TextUnformatted(plug.asr.ui.status.c_str());
									ImGui::PopStyleColor();
									plug.asr.ui.status = "Asr running normally";
								}
							}

							ImGui::PopTextWrapPos();
							if (at_bottom) {
								ImGui::SetScrollHereY(1.0f);
							}
							ImGui::EndChild();
							ImGui::Checkbox("at_bottom", &at_bottom);
							ImGui::SliderInt("max_history_bytes", &max_history_bytes, 256, 0x2FFF);
						}
						break;
					}// asr
					while (ImGui::TreeNode("translator")) {
						DEFER{ ImGui::TreePop };
						if (!get(ui.plugin.allow_translator)) {
							ImGui::Text("disabled by host");
							break;
						}
						ImGui::PushIdGuard pushIdGuard(__LINE__);
						auto [on, changed] = ::loadPlugin("plugin/translator", plug.translator.dir, this, translatorOn<false>(), &ClientApp::translatorLoad, &ClientApp::translatorDownload);
						if (on) {
							if (changed) [[unlikely]] {
								threadTranslate = std::make_unique<std::jthread>(&ClientApp::translateThread, this);
							}

							std::string now_label = plug.translator.direction.src + " -> " + plug.translator.direction.tar;
							if (ImGui::BeginCombo("Translation Direction", now_label.c_str())) {
								for (const auto& dir : plug.translator.directions) {
									std::string item_label = dir.src + " -> " + dir.tar;

									bool is_selected = plug.translator.direction == dir;

									if (ImGui::Selectable(item_label.c_str(), is_selected)) {
										if (!is_selected) [[likely]] {
											char* error{};
											if (plug.translator.unload) [[likely]] {
												std::lock_guard lock0{ plug.translator.mtx };
												std::lock_guard lock1{ plug.translator.mtx_ui };
												plug.translator.unload(plug.translator.direction.src.c_str(), plug.translator.direction.tar.c_str(), &error);
											}
											{
												std::lock_guard lock1{ plug.translator.mtx_ui };
												plug.translator.direction = dir;
											}
											if (error) {
												translatorSetError<true>(error);
											}
										}
									}

									if (is_selected) {
										ImGui::SetItemDefaultFocus();
									}
								}
								ImGui::EndCombo();
							}


							ImGui::BeginChild("##translate", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
							ImGui::PushTextWrapPos(0.0f);
							
							if (!asrOn<true>()) {
								translatorSetError<true>("load asr before load translator");
							}

							{
								std::lock_guard lock{ plug.translator.mtx_ui };
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
								ImGui::TextUnformatted(plug.translator.result.c_str());
								ImGui::PopStyleColor();
							}

							{
								std::lock_guard lock{ plug.translator.mtx_ui };
								if (!plug.translator.error.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
										ImGui::TextUnformatted(plug.translator.error.c_str());
										plug.translator.error.clear();
									ImGui::PopStyleColor();
								}
							}

							ImGui::PopTextWrapPos();
							ImGui::EndChild();
						}

						break;
					}// translator
					while (ImGui::TreeNode("summary")) {
						DEFER{ ImGui::TreePop };

						if (!get(ui.plugin.allow_summary)) {
							ImGui::Text("disabled by host");
							break;
						}
						ImGui::PushIdGuard pushIdGuard(__LINE__);
						auto [on, changed] = ::loadPlugin("plugin/summary", plug.summary_dir, this, summaryOn<false>(), &ClientApp::summaryLoad, &ClientApp::summaryDownload);
						(void)changed;   // 加载/卸载都由 summaryLoad / summaryDownload 管理
						auto st = plug.summary;   // 本帧使用的实例；download 之后这里会变成空
						if (on && st) {
							ImGui::SelectFile("model_name", fs::path{ st->dir } / "models", mid.summary_model);
							{
								std::lock_guard lk_ui{ st->ui.mtx };
								st->ui.src = mid.summary_model;
							}
							if (!mid.summary_model.empty() &&
								!st->ready() &&
								ImGui::Button("load_model")
							) {
								st->busy.release();
							}

							if (st->generating.load()) {
								const auto now = std::chrono::steady_clock::now().time_since_epoch();
								const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
								const long long started = st->gen_start_ms.load();
								const long long elapsed = (started > 0 ? (ms - started) : 0);
								ImGui::Text((char*)u8"正在总结… %.1f s", elapsed / 1000.0);
								// 超时取消放在 UI 线程做：此时实例一定还活着，
								// 不需要在工作线程里保存函数指针副本。
								if (!st->cancel_sent.load() && elapsed > 90000) {
									st->cancel_sent = true;
									if (st->cancel) { st->cancel(); }
									std::cout << "[summary] generate timeout(90s), requesting cancel\n";
								}
							}
							else if (st->ready() && ImGui::Button("summary")) {
								{
									// 内容由 UI 线程取好放进实例，工作线程不再访问 plug.asr
									std::lock_guard lk_ui{ st->ui.mtx };
									std::lock_guard lk_asr{ plug.asr.ui.mtx };
									st->ui.content = plug.asr.ui.history;
								}
								st->busy.release();
							}


							ImGui::BeginChild("##translate", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
							ImGui::PushTextWrapPos(0.0f);

							{
								std::lock_guard lock{ st->ui.mtx };
								if (!st->ui.result.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
									ImGui::TextUnformatted(st->ui.result.c_str());
									ImGui::PopStyleColor();
								}
								if (!st->ui.error.empty()) {
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
									ImGui::TextUnformatted(st->ui.error.c_str());
									ImGui::PopStyleColor();
								}
							}

							ImGui::PopTextWrapPos();
							ImGui::EndChild();
						}
						else if (on && !plug.summary_error.empty()) {
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
							ImGui::TextUnformatted(plug.summary_error.c_str());
							ImGui::PopStyleColor();
						}
						break;
					}// summary
					ImGui::TreePop();
				}// plugin

				ImGui::End();
			}// settings


				// ============ main window ============ 
			style->Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
			
			//////////////////////////// camera
			// camTextures.size(chosen_users.size()), for each of camTextures
			{
				std::lock_guard lock{ ui.room_user.mtx_chosen_users };
				::resize_emplace(camTextures, ui.room_user.chosen_users.size(), 1, 1, std::vector<uint8_t>{ 0,0,0,0 }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
				//camTextures.resize(ui.room_user.chosen_users.size());
				size_t i = 0;
				std::string user_name{};
				for (const auto& chosen_user : ui.room_user.chosen_users) {
					const user::id_t target_id = chosen_user.first;
					auto& total_camera_frames = chosen_user.second->total_camera_frames;
					//auto& camTexture = chosen_user.second->camTexture;
					auto& cam_frame = chosen_user.second->cam_frame;
					auto& camTexture = camTextures[i];
					const auto rv_cam_orig_w = get(chosen_user.second->rv_cam_orig_w);
					const auto rv_cam_orig_h = get(chosen_user.second->rv_cam_orig_h);

					//const user::id_t target_id = get(ui.room_user.chosen_user);
					bool target_share_camera{};
					{
						std::lock_guard lock(mtx_users);
						auto user = users.find(target_id);
						user_name.clear();
						assert(user != users.end());
						target_share_camera = get(user->second.share_camera);
						if (target_share_camera) {
							user_name = user->second.name;
						}
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
						ImGui::Begin(std::format("camera-{}###camera-{}", users[target_id].name, i).c_str(), nullptr, ImGuiWindowFlags_NoScrollbar);
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
					++i;
				}
			}

			// same as camTextures
			{
				std::lock_guard lock{ ui.room_user.mtx_chosen_users };
				std::size_t i = 0;
				::resize_emplace(imgs, ui.room_user.chosen_users.size(), 1, 1, std::vector<uint8_t>{ 0x33, 0x33, 0x33, 0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
				std::string user_name;
				for (const auto& chosen_user : ui.room_user.chosen_users) {
					auto& img = imgs[i];
					const user::id_t target_id = chosen_user.first;
					auto& total_video_frames_v1 = chosen_user.second->total_video_frames_v1;
					//auto& img = chosen_user.second->img;
					auto& decodedFrame = chosen_user.second->decodedFrame;
					const auto rv_sc_orig_w = get(chosen_user.second->rv_sc_orig_w);
					const auto rv_sc_orig_h = get(chosen_user.second->rv_sc_orig_h);

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


					bool share_video{};
					if (decodedFrame && width > 0 && height > 0 && data->size() == (size_t)width * height * 4) {
						user_name.clear();
						{
							std::lock_guard lock(mtx_users);
							auto& user = users[target_id];
							share_video = user.share_video;
							if (share_video) {
								user_name = user.name;
							}
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
					
					if (share_video) {
						ImGui::Begin(std::format("screen-{}###screen-{}", user_name, i).c_str(), nullptr, ImGuiWindowFlags_NoScrollbar);
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
						ImGui::End();// main
					}

					++i;
				}
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
				windowClosed = true;
			}
			glfwPollEvents();
			renderDealing();
		}

		// 离开 Show 前先把 summary 线程收掉（最多等 3 秒，不会把窗口退出卡在推理上）
		summaryDownload();

		client.Close();
		client_audio.Close();

	}
	println("asr downloading");
	asrDownload();
	println("asr downloaded");

	println("translate downloading");
	translatorDownload();
	println("translate downloaded");

	println("show ended");
	if (windowClosed) {
		return Page::Tmp;
	}

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