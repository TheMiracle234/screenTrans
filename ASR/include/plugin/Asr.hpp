#pragma once
#include <plugin/asr.h>
namespace plugin {
	namespace fn {
		using AsrInit = decltype(&plugin_asr_init);
		using AsrTranscribe = decltype(&plugin_asr_transcribe);
		using AsrFree = decltype(&plugin_asr_free);
	}
	namespace name {
#	define STR(x) #x
		constexpr const char* asrInit = STR(plugin_asr_init);
		constexpr const char* asrTranscribe = STR(plugin_asr_transcribe);
		constexpr const char* asrFree = STR(plugin_asr_free);
#	undef STR
	}
}