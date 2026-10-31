// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_ADDRINFO_HPP
#define NET_ADDRINFO_HPP

// 依赖 Socket.hpp：保证 Winsock2.h 先于 ws2tcpip.h 被包含，
// 并复用 net 命名空间下的 ip_t / Ip / Socket。
#include "net/Socket.hpp"

#ifdef _WIN32
#	include <ws2tcpip.h>   // getaddrinfo / freeaddrinfo / gai_strerrorA（幂等，Socket.hpp 已含）
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

namespace net {

	// RAII 封装 getaddrinfo() 返回的 addrinfo* 链表。
	// - 不可拷贝，可移动
	// - begin()/end() 遍历 ai_next 链，可直接 range-for
	// - 解析失败时返回 std::nullopt，用 getLastError() 取 gai_strerror 文本
	// - 错误空间与 Socket::last_err 隔离：getaddrinfo 的 EAI_* / WSAHOST_* 不是
	//   errno / WSA 错误码，不能传给 Socket::CheckClosedByErrorCode
	class AddrInfo
	{
	public:
		class iterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = addrinfo;
			using difference_type = std::ptrdiff_t;
			using pointer = const addrinfo*;
			using reference = const addrinfo&;

			iterator() noexcept = default;
			explicit iterator(pointer p) noexcept : p_(p) {}

			reference operator*()  const noexcept { return *p_; }
			pointer   operator->() const noexcept { return p_; }

			iterator& operator++()    noexcept { p_ = p_->ai_next; return *this; }
			iterator  operator++(int) noexcept { iterator t = *this; ++*this; return t; }

			friend bool operator==(const iterator&, const iterator&) = default;

		private:
			pointer p_ = nullptr;
		};

		AddrInfo() noexcept = default;
		~AddrInfo() { Reset(); }

		AddrInfo(AddrInfo&& other) noexcept : head_(other.head_) { other.head_ = nullptr; }

		AddrInfo& operator=(AddrInfo&& other) noexcept
		{
			if (this == &other) return *this;
			Reset();
			head_ = other.head_;
			other.head_ = nullptr;
			return *this;
		}

		AddrInfo(const AddrInfo&) = delete;
		AddrInfo& operator=(const AddrInfo&) = delete;

		// 调用 getaddrinfo() 查询地址。
		//   node     : 主机名 / 点分十进制 / 空串或 nullptr（配合 AI_PASSIVE 表示"本机任意地址"）
		//   port     : 端口（host 序），0 表示"由系统分配"
		//   family   : AF_UNSPEC / AF_INET / AF_INET6；可传 static_cast<ip_t>(Ip::Auto) 等
		//   socktype : SOCK_STREAM / SOCK_DGRAM ...
		//   protocol : IPPROTO_TCP / IPPROTO_UDP ...
		//   flags    : AI_PASSIVE / AI_NUMERICHOST / ...（AI_NUMERICSERV 会被自动 OR 上）
		//
		// 返回 std::nullopt 表示 getaddrinfo() 未给出任何候选地址（rc != 0 或链表为空），
		// 不是 socket/bind/connect 层面的失败。
		static std::optional<AddrInfo> Resolve(
			const char* node,
			uint16_t    port,
			int         family = AF_UNSPEC,
			int         socktype = SOCK_STREAM,
			int         protocol = IPPROTO_TCP,
			int         flags = 0);

		bool Empty() const noexcept { return head_ == nullptr; }
		const addrinfo* Head() const noexcept { return head_; }

		iterator begin() const noexcept { return iterator{ head_ }; }
		iterator end()   const noexcept { return iterator{ nullptr }; }

		static std::string& getLastError() noexcept { return last_err; }

	private:
		void Reset() noexcept
		{
			if (head_) { ::freeaddrinfo(head_); head_ = nullptr; }
		}

		explicit AddrInfo(addrinfo* head) noexcept : head_(head) {}

		addrinfo* head_ = nullptr;

		// 与 Socket::last_err 分开，避免混淆两种错误空间
		static inline thread_local std::string last_err{};
	};

	inline std::optional<AddrInfo> AddrInfo::Resolve(
		const char* node,
		uint16_t    port,
		int         family,
		int         socktype,
		int         protocol,
		int         flags)
	{
		last_err.clear();

		char port_str[6] = {};   // 0..65535 -> 最多 5 位 + '\0'
		std::snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port));

		addrinfo hints{};
		hints.ai_family = family;
		hints.ai_socktype = socktype;
		hints.ai_protocol = protocol;
		// port 永远是数字串，强制 AI_NUMERICSERV，避免某些系统去查 /etc/services
		hints.ai_flags = flags | AI_NUMERICSERV;

		addrinfo* result = nullptr;
		int rc = ::getaddrinfo(node, port_str, &hints, &result);
		if (rc != 0 || result == nullptr) {
#ifdef _WIN32
			last_err = gai_strerrorA(rc);
#else
			if (rc == EAI_SYSTEM) {
				last_err = std::string("getaddrinfo: ") + std::strerror(errno);
			}
			else {
				last_err = gai_strerror(rc);
			}
#endif
#ifndef NDEBUG
			std::cerr << "getaddrinfo() failed: " << last_err << '\n';
#endif
			return std::nullopt;
		}

		return AddrInfo{ result };
	}

} // namespace net

#endif // NET_ADDRINFO_HPP