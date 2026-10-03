#include <windows.h>
#include <string>
namespace wins {
    inline std::string WideToUtf8(const std::wstring_view w) {
        if (w.empty()) return {};

        int size = WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), (int)w.size(),
            nullptr, 0,
            nullptr, nullptr
        );

        std::string s(size, '\0');

        WideCharToMultiByte(
            CP_UTF8, 0,
            w.data(), (int)w.size(),
            s.data(), size,
            nullptr, nullptr
        );

        return s;
    }

    inline std::wstring Utf8ToWide(const std::string_view s) {
        if (s.empty()) return {};

        int size = MultiByteToWideChar(
            CP_UTF8, 0,
            s.data(), (int)s.size(),
            nullptr, 0
        );

        std::wstring w(size, L'\0');

        MultiByteToWideChar(
            CP_UTF8, 0,
            s.data(), (int)s.size(),
            w.data(), size
        );

        return w;
    }
}