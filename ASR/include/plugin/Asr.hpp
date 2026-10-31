#pragma once
#include <plugin/asr.h>
#include <string.h>
#include <mutex>

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
        std::string history;
        std::string partial;
        std::string status;
        uint64_t    current_utterance = 0;
        bool        dirty = false;
    };

    static void drain_events(plugin_asr_stream* s, plugin::asr::fn::AsrPoll pfn_poll, AsrUi& ui) {
        std::vector<char> buf(65536);
        plugin_asr_result r{};
        r.struct_size = sizeof(r);

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
                ui.dirty = true;
                break;

            case PLUGIN_ASR_FINAL: {
                std::string t = buf.data();
                if (!t.empty()) {
                    ui.history += t;
                    if (!ui.history.empty() && ui.history.back() != '\n') ui.history += '\n';
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