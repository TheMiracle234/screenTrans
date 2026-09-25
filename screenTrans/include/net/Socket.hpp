// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_SOCKET_HPP
#define NET_SOCKET_HPP

#ifdef _WIN32
#	define _WINSOCK_DEPRECATED_NO_WARNINGS
#	include <WinSock2.h>
#	pragma comment(lib, "ws2_32.lib")
#endif //_WIN32

#include <string>
#include <cstdint>
#include <mutex>

namespace net {

#ifdef _WIN32
	using socket_t = SOCKET;
	constexpr socket_t invalid_socket = INVALID_SOCKET;
#endif // _WIN32

#undef SOCKET
#undef INVALID_SOCKET
#define SOCKET static_assert(false, "use net::socket_t instead")
#define INVALID_SOCKET static_assert(false, "use net::invalid_socket instead")

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

	protected:
#		define NET_SOCKET_SET_ERROR(str) Socket::SetError(str, __FILE__, __LINE__)
		static void SetError(std::string_view str, std::string_view file, int line);
		static bool StartUp();
		static void CleanUp();
		static bool CheckClosedByErrorCode(int code);

	public:
		static bool Check(bool ok);

	public:
		socket_t id = invalid_socket;

		static std::string& getLastError() { return last_err; }

		Socket();
		Socket(const Socket& other) = delete;
		Socket& operator=(const Socket& other) = delete;
		Socket(Socket&& other) noexcept;
		Socket& operator=(Socket&& other) noexcept;
		~Socket();

		void Close();
		bool Closed() { return id == invalid_socket; }
	};

	struct InitGuard {
		InitGuard() { Socket::StartUp(); }
		~InitGuard() { Socket::CleanUp(); }
	};
}

#endif