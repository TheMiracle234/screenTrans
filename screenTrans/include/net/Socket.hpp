// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_SOCKET_HPP
#define NET_SOCKET_HPP

#ifdef _WIN32
#	define _WINSOCK_DEPRECATED_NO_WARNINGS
#	include <WinSock2.h>
#	pragma comment(lib, "ws2_32.lib")
#else
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
#	define WSAGetLastError static_assert(false);
#	define SOCKADDR_IN static_assert(false);
#	define SOCKADDR static_assert(false);
#	undef INVALID_SOCKET
#	undef SOCKET_ERROR
#else
	using socket_t = int;
	inline constexpr socket_t invalid_socket = -1;
	inline constexpr int socket_error = -1;
	inline int last_socket_error() { return errno; }
#endif // _WIN32

	namespace tcp {
		class Client;
		class Server;
		using ip_t = int;
		enum class Ip : ip_t {
			v4 = AF_INET,
			v6 = AF_INET6,
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
		Socket(socket_t id);
		Socket(const Socket& other) = delete;
		Socket& operator=(const Socket& other) = delete;
		Socket(Socket&& other) noexcept;
		Socket& operator=(Socket&& other) noexcept;
		~Socket();

		socket_t id() { return m_id; }
		void Close();
		bool Closed() { return m_id == invalid_socket; }
	private:
		socket_t m_id = invalid_socket;
	};

	struct InitGuard {
		InitGuard() { Socket::StartUp(); }
		~InitGuard() { Socket::CleanUp(); }
	};
}

#endif