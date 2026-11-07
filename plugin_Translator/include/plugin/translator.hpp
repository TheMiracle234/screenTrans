#pragma once
#include <plugin/translator.h>
namespace plugin {
namespace translator {
	namespace fn {
		using Register = decltype(&plugin_translator_register);
		using unload = decltype(&plugin_translator_unload);
		using Translate = decltype(&plugin_translator_translate);
		using Close = decltype(&plugin_translator_close);
	}

	namespace name {
#define STR(fn) #fn
		constexpr const char* Register = STR(plugin_translator_register);
		constexpr const char* unload = STR(plugin_translator_unload);
		constexpr const char* translate = STR(plugin_translator_translate);
		constexpr const char* close = STR(plugin_translator_close);
#undef STR
	}
}
}