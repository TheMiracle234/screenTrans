#ifndef TMSOCKET_H
#define TMSOCKET_H

#ifndef NDEBUG
#	define SOCK_DEBUG
#endif

#define TM_SOCKET_STATIC

#ifdef TM_SOCKET_STATIC
#	define TM_SOCKET_API
#else
#	ifdef TM_SOCKET_EXPORT
#		define TM_SOCKET_API __declspec(dllexport)
#	else
#		define TM_SOCKET_API __declspec(dllimport)
#	endif
#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <string>
#include <cstdint>
#include <mutex>

namespace TM {

	class Client;
	class Server;
	//为保证线程安全，生命周期完全串行
	class TM_SOCKET_API Socket
	{
		friend class Client;
		friend class Server;

	private:
		static inline thread_local std::string last_err = "";
		static inline std::mutex mtx_obj_life;
		static inline int count = 0; // count of obj

	protected:

#		define TM_SOCKET_SET_ERROR(str) Socket::SetError(str, __FILE__, __LINE__)
		static void SetError(std::string_view str, std::string_view file, int line);
		static bool StartUp();
		static void CleanUp();
		static bool CheckClosedByErrorCode(int code);

	public:
		static bool Check(bool ok);

	public:
		enum Protocol {
			TCP = SOCK_STREAM,
			//UDP = SOCK_DGRAM,
		};

		enum Ip {
			IPV4 = AF_INET,
			IPV6 = AF_INET6,
		};

		SOCKET id = INVALID_SOCKET;

		static std::string& getLastError() { return last_err; }

		Socket();
		Socket(const Socket& other) = delete;
		Socket(Socket&& other) noexcept : id(other.id) { std::lock_guard lock(mtx_obj_life); ++count; other.id = INVALID_SOCKET; }
		Socket& operator=(Socket&& other) noexcept { id = other.id; other.id = INVALID_SOCKET; return *this; }
		~Socket();

		void Close();
		bool Closed() { return id == INVALID_SOCKET; }
	};
}

#endif