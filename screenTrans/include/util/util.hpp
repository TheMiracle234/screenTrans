#pragma once

#include <utility>
#include <string>
#include <string_view>
#include <fstream>
#include <filesystem>
#include <cstddef>
#include <chrono>

#include <wins/wc_u8.hpp>

template<typename Fn>
struct Defer {
	Fn m_fn;
	Defer(Fn fn) : m_fn(std::move(fn)) {}
	Defer(const Defer&) = delete;
	Defer(Defer&&) = delete;
	~Defer() { m_fn(); }
};

#define XCONCAT(x, y) x##y
#define CONCAT(x, y) XCONCAT(x, y) 
#define NAME(name) CONCAT(CONCAT(name##_, __LINE__), __)
#define DEFER Defer NAME(defer)

template<class T>
struct Record{
	T now;
	T pre;
	explicit Record(const T& v) : now(v), pre(now) {}
	explicit Record(T&& v) : now(std::move(v)), pre(now) {}
	void update(const T& v) {
		pre = now;
		now = v;
	}
	void update(T&& v) {
		pre = std::move(now);
		now = std::move(v);
	}
	bool changed() { return now != pre; }
};

inline std::string makeRecordPath(std::string_view dir, std::string_view postfix) {
	using namespace std::chrono;
	auto now = system_clock::now();
	auto t = floor<seconds>(now);
	auto zt = zoned_time{ current_zone(), t };     // 本地时区
	auto local = zt.get_local_time();
	auto ymd = year_month_day{ floor<days>(local) };
	auto tod = hh_mm_ss<seconds>{ local - floor<days>(local) };

	std::string path = std::format("{}/{:04}-{:02}-{:02}_{:02}-{:02}-{:02}{}",
		dir,
		int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()),
		tod.hours().count(), tod.minutes().count(), tod.seconds().count(),
		postfix
		);

	// 顺手确保目录存在
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return path;
}

inline bool saveFile(std::string_view path, const void* data, std::size_t bytes) {
	std::ofstream file(std::string(path), std::ios::binary);
	if (!file)
		return false;
	file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
	return file.good();
}

inline [[nodiscard]] bool file_append(std::string_view path, const void* data, size_t bytes) noexcept
{
	if (bytes == 0)
		return true;

	// std::ios::app 标准追加模式，每次写都到末尾；std::ios::binary 关闭文本转义(\n<->\r\n)
	std::ofstream f{ path.data(), std::ios::app | std::ios::binary };
	if (!f.is_open())
		return false;

	// write 接收 const char*，强制转换
	f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));

	// 检查两点：流无错误，并且写入数量匹配
	if (!f)
		return false;

	f.close();
	return true;
}

inline [[nodiscard]] bool file_append(std::wstring_view path, const void* data, size_t bytes) noexcept {
	return ::file_append(wins::WideToUtf8(path), data, bytes);
}

inline [[nodiscard]] std::string read_all_file(std::string_view path) noexcept
{
	std::ifstream f{ path.data(), std::ios::in | std::ios::binary };
	if (!f.is_open())
		return {};

	// 一次性读取全部：先取大小，预分配内存，一次read
	f.seekg(0, std::ios::end);
	const auto sz = f.tellg();
	if (sz < 0)
		return {};

	std::string buf;
	buf.resize(static_cast<size_t>(sz));

	f.seekg(0, std::ios::beg);
	f.read(buf.data(), static_cast<std::streamsize>(sz));

	if (!f)
		buf.clear();

	return buf;
}