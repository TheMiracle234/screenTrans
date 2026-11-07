#pragma once
#include <Windows.h>
#include <string>
#include <string_view>
#include <cassert>
#include <filesystem>
#include <type_traits>
#include <wins/wc_u8.hpp>

#ifdef SCREEN_TRANS_PRJ
#   error "always inline"
#endif

namespace plugin {
    class Loader {
    public:
        Loader() = default;

        bool load(const char* u8_path) {
            free();
            if (!u8_path || !*u8_path) return false;

            // LOAD_WITH_ALTERED_SEARCH_PATH 要求绝对路径，
            // 否则依赖搜索仍走默认顺序，126 照旧。
            std::error_code ec;
            std::filesystem::path p{ wins::Utf8ToWide(u8_path) };
            p = std::filesystem::absolute(p, ec);
            if (ec) return false;
            p = p.lexically_normal();

            // 让「本 DLL 所在目录」成为依赖搜索的第一站。
            h = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            return h != nullptr;
        }

        template<typename Fn>
        bool get(Fn& out, const char* func) {
            static_assert(std::is_pointer_v<Fn>, "Fn must be a function pointer");
            assert(on());
            out = reinterpret_cast<Fn>(GetProcAddress(h, func));
            return out != nullptr;
        }

        void free() {
            if (h) {
                auto save = h;
                h = nullptr;
                FreeLibrary(save);
            }
        }

        ~Loader() { free(); }

    private:
        bool on() const { return h != nullptr; }
        HMODULE h{};
    };
}