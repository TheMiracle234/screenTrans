#pragma once
#include <plugin/summary.h>

namespace plugin {
namespace summary {
	namespace fn {
		using start = decltype(&plugin_summary_start);
		using ready = decltype(&plugin_summary_ready);
		using cancel = decltype(&plugin_summary_cancel);
		using generate = decltype(&plugin_summary_generate);
		using lasterror = decltype(&plugin_summary_lasterror);
		using stop = decltype(&plugin_summary_stop);
	}

	namespace name {
#define STR(fn) #fn
		constexpr const char* start = STR(plugin_summary_start);
		constexpr const char* ready = STR(plugin_summary_ready);
		constexpr const char* cancel = STR(plugin_summary_cancel);
		constexpr const char* generate = STR(plugin_summary_generate);
		constexpr const char* lasterror = STR(plugin_summary_lasterror);
		constexpr const char* stop = STR(plugin_summary_stop);
#undef STR
	}
}
}