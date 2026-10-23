#pragma once
#include <cstdlib>
#include <string>
#include <filesystem>
namespace flexmeet {
    inline std::string environment(const char* key) {
#ifdef _WIN32
        const std::wstring wide_key(key, key + std::char_traits<char>::length(key));
        wchar_t* p = nullptr; size_t size = 0;
        if (_wdupenv_s(&p, &size, wide_key.c_str()) != 0 || !p) return {};
        const auto bytes = std::filesystem::path(p).u8string();
        std::string value(reinterpret_cast<const char*>(bytes.data()), bytes.size()); std::free(p); return value;
#else
        const char* p = std::getenv(key); return p ? p : "";
#endif
    }
}