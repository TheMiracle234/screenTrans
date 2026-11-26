#pragma once
#include <string>
#include <vector>
#include <mutex>

#include <util/Timer.hpp>
#include <plugin/asr.h>

namespace plugin {
namespace asr {
	namespace fn {
		using AsrOpen = decltype(&plugin_asr_open);
		using AsrPush = decltype(&plugin_asr_push);
		using AsrPoll = decltype(&plugin_asr_poll);
		using AsrFinish = decltype(&plugin_asr_finish);
		using AsrReset = decltype(&plugin_asr_reset);
		using AsrClose = decltype(&plugin_asr_close);
	}

	namespace name {
#define STR(fn) #fn
		constexpr const char* asrOpen = STR(plugin_asr_open);
		constexpr const char* asrPush = STR(plugin_asr_push);
		constexpr const char* asrPoll = STR(plugin_asr_poll);
		constexpr const char* asrFinish = STR(plugin_asr_finish);
		constexpr const char* asrReset = STR(plugin_asr_reset);
		constexpr const char* asrClose = STR(plugin_asr_close);
#undef STR
	}

    struct AsrUi {
        std::mutex  mtx;
        util::Timer timerStay;
        std::string history;
        std::string partial;
        std::string partialStay;
        std::string status;
        uint64_t    current_utterance = 0;
        bool        dirty = false;
    };

    inline void drain_events(PLUGIN_ASR_stream* s, plugin::asr::fn::AsrPoll pfn_poll, AsrUi& ui) {
        std::vector<char> buf(65536);
        PLUGIN_ASR_result r{};
        r.struct_size = sizeof(r);

        {
            std::lock_guard lk(ui.mtx);
            ui.dirty = false;

            // ★ 超时清除放在循环之前：先清掉上一帧遗留的过期 partialStay，
            //   再让本帧的新事件重新设置它。这样同一帧里 FINAL 设的 partialStay
            //   不会被"刚刚越线"的超时检查误清。
            constexpr float staySeconds = 2.0f;
            if (!ui.partialStay.empty() && ui.timerStay.seconds() > staySeconds) {
                ui.partialStay.clear();
            }
        }

        for (int iter = 0; iter < 256; ++iter) {
            int32_t st = pfn_poll(s, &r, buf.data(), (uint32_t)buf.size());

            if (st == PLUGIN_ASR_EMPTY || st == PLUGIN_ASR_OK) break;

            if (st == PLUGIN_ASR_BUFFER_SMALL) {
                if (r.text_bytes + 1 > buf.size()) buf.resize(r.text_bytes + 1);
                continue;
            }

            if (st != PLUGIN_ASR_EVENT) {
                std::lock_guard lk(ui.mtx);
                ui.status = "ASR Poll Error: " + std::to_string(st);
                ui.dirty = true;
                break;
            }

            std::lock_guard lk(ui.mtx);
            switch (r.type) {
            case PLUGIN_ASR_PARTIAL:
                if (r.utterance_id != ui.current_utterance) {
                    if (!ui.partial.empty()) {
                        ui.history += ui.partial;
                        if (!ui.history.empty() && ui.history.back() != '\n') ui.history += '\n';
                    }
                    ui.current_utterance = r.utterance_id;
                    ui.partial.clear();
                }
                ui.partial = buf.data();
                // 只有内容变化时才更新 partialStay + 续命；
                // 重复相同 partial 不续命，避免静音时引擎抖动把 timer 无限推后。
                if (!ui.partial.empty() && ui.partial != ui.partialStay) {
                    ui.partialStay = ui.partial;
                    ui.timerStay.restart();
                }
                ui.dirty = true;
                break;

            case PLUGIN_ASR_FINAL: {
                std::string t = buf.data();
                if (!t.empty()) {
                    ui.history += t;
                    if (!ui.history.empty() && ui.history.back() != '\n') ui.history += '\n';
                    // ★ FINAL 文本也留在屏上 1.5s，解决"还没 partial 就 history"，
                    //   同时让短句（PARTIAL+FINAL 同一次 poll）也有停留时间。
                    ui.partialStay = std::move(t);
                    ui.timerStay.restart();
                }
                if (r.utterance_id == ui.current_utterance) ui.partial.clear();
                ui.dirty = true;
                break;
            }

            case PLUGIN_ASR_GAP:
                ui.status = "ASR Audio Overflow. Some data dropped.";
                ui.dirty = true;
                break;

            case PLUGIN_ASR_ERROR:
                ui.status = "ASR Error: ";
                ui.status += buf.data();
                ui.dirty = true;
                break;

            case PLUGIN_ASR_END:
                break;
            }
        }
    }
}
}