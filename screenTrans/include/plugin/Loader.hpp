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
			free();
			h = LoadLibraryW(wins::Utf8ToWide(u8_path).c_str());
			return h != nullptr;
		}
		template<typename Fn>
		bool get(Fn& out, const char* func) {
			assert(on());
			return (out = reinterpret_cast<Fn>(GetProcAddress(h, func)));
		}
		void free() {
			if (h) {
				auto save = h;
				h = nullptr;
				FreeLibrary(save);
			}
		}
		~Loader() {
			free();
		}
	private:
		bool on() const { return h != nullptr; }
		HMODULE h{};
	};
}