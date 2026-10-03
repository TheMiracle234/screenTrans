#pragma once
#include <Windows.h>
#include <string>
#include <string_view>
#include <cassert>
#include <wins/wc_u8.hpp>
#ifdef SCREEN_TRANS_PRJ
#	error "always inline"
#endif
namespace plugin {
	class Loader {
	public:
		Loader() = default;
		bool load(const char* u8_path) {
			if (h) { FreeLibrary(h); }
			return static_cast<bool>(h = LoadLibraryW(wins::Utf8ToWide(u8_path).c_str()));
		}
		template<typename Fn>
		bool get(Fn& out, const char* func) {
			assert(h);
			return (out = reinterpret_cast<Fn>(GetProcAddress(h, func)));
		}
		~Loader() {
			if (h) { FreeLibrary(h); }
		}
	private:
		HMODULE h{};
	};
}