// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_SOCKET_HPP
#define NET_SOCKET_HPP

#ifdef _WIN32
#	define _WINSOCK_DEPRECATED_NO_WARNINGS
#	include <WinSock2.h>
#	include <ws2tcpip.h>   // getaddrinfo / freeaddrinfo / gai_strerror
#	pragma comment(lib, "ws2_32.lib")
#else
#	include <netinet/tcp.h>   // ← TCP_NODELAY
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <netdb.h>
#	include <unistd.h>
#	include <cerrno>
#	include <cstring>
#endif

#include <string>
#include <cstdint>
#include <mutex>

namespace net {

#ifdef _WIN32
	using socket_t = SOCKET;
	using socklen_t = int;
	constexpr socket_t invalid_socket = INVALID_SOCKET;
	inline constexpr int socket_error = SOCKET_ERROR;
	inline int last_socket_error() { return WSAGetLastError(); }
#else
	using socket_t = int;
	inline constexpr socket_t invalid_socket = -1;
	inline constexpr int socket_error = -1;
	inline int last_socket_error() { return errno; }
#endif // _WIN32

	// 关闭 Nagle：小包立即发出，避免 "长度头 4B + 负载" 被延迟 ACK 卡住
	inline bool set_tcp_nodelay(socket_t id) {
		if (id == invalid_socket) return false;
		int on = 1;
		// Windows 要求 const char*，Linux 要求 const void* —— 传 const char* 两边都能编译
		// （和 Server.cpp 里 SO_REUSEADDR 的写法保持一致）
		return setsockopt(id, IPPROTO_TCP, TCP_NODELAY,
			reinterpret_cast<const char*>(&on),
			static_cast<socklen_t>(sizeof(on))) == 0;
	}

	namespace tcp {
		class Client;
		class Server;
		using ip_t = int;
		enum class Ip : ip_t {
			v4 = AF_INET,
			v6 = AF_INET6,
			Auto = AF_UNSPEC,
		};
	}

	struct InitGuard;

	class Socket
	{
		friend class tcp::Client;
		friend class tcp::Server;
		friend struct InitGuard;

	private:
		static inline thread_local std::string last_err = "";
#	ifndef NDEBUG
		static inline bool started = false;
#	endif

	private:
#		define NET_SOCKET_SET_ERROR(str) Socket::SetError(str, __FILE__, __LINE__)
		static void SetError(std::string_view str, std::string_view file, int line);
		static bool StartUp();
		static void CleanUp();
		static bool CheckClosedByErrorCode(int code);

	public:
		static bool Check(bool ok);
		static std::string& getLastError() { return last_err; }
		Socket() = default;
		Socket(socket_t id);
		Socket(const Socket& other) = delete;
		Socket& operator=(const Socket& other) = delete;
		Socket(Socket&& other) noexcept;
		Socket& operator=(Socket&& other) noexcept;
		~Socket();

		socket_t id() { return m_id; }
		void Close();
		[[nodiscard]] bool Closed() { return m_id == invalid_socket; }
	private:
		socket_t m_id = invalid_socket;
	};

	struct InitGuard {
		InitGuard() { Socket::StartUp(); }
		~InitGuard() { Socket::CleanUp(); }
	};
}

#endif