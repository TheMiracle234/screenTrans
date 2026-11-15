#pragma once

#include <utility>

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