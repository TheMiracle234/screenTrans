#include "Socket.h"

#include <iostream>

namespace TM {
	Socket::Socket() {
		std::lock_guard lock(mtx_obj_life);
		if (count == 0) {
			Socket::StartUp();
		}
		++count;
	}

	void Socket::SetError(std::string_view str, std::string_view file, int line) {
		last_err = std::string(file) + "\nline " + std::to_string(line) + ": " + std::string(str) + "\n";
#	ifdef SOCK_DEBUG
		std::cerr << last_err << std::endl;
#	endif	
	}

	bool Socket::StartUp()
	{
		WSADATA wsaData;
		int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result != 0) {
			TM_SOCKET_SET_ERROR("WSAStartup failed with error code: " + std::to_string(result));
			return false;
		}
		return true;
	}

	void Socket::CleanUp()
	{
		WSACleanup();
	}

	bool Socket::CheckClosedByErrorCode(int code) {
		switch (code) {
			// 对端重置连接（已不可用）
		case WSAECONNRESET:
			// 套接字未连接
		case WSAENOTCONN:
			// 连接超时
		case WSAETIMEDOUT:
			// 本地中止连接
		case WSAECONNABORTED:
			// 套接字已关闭
		case WSAESHUTDOWN:
			// 网络子系统失效
		case WSAENETDOWN:
			// 网络连接被重置
		case WSAENETRESET:

			// 连接被拒绝（端口未监听等）
		case WSAECONNREFUSED:
			// 网络不可达
		case WSAENETUNREACH:
			// 主机不可达
		case WSAEHOSTUNREACH:
			// 缓冲区空间不足（系统资源不足，连接已无法维持）
		case WSAENOBUFS:
			// 操作不支持（协议状态异常，连接不可用）
		case WSAEOPNOTSUPP:

			// 对端正常关闭（可选，表示连接已断开）
		case WSAEDISCON:

			// not socket（套接字已关闭或无效）
		case WSAENOTSOCK:

			// 注意：WSAEINTR (中断) 一般不需要关闭套接字，这里不加入
			return true;
		default:
			return false;
		}
	}

	bool Socket::Check(bool ok)
	{
		if (!ok) {
			std::cerr << last_err << std::endl;
		}
		return ok;
	}

	Socket::~Socket() {
		std::lock_guard lock(mtx_obj_life);
		if (id != INVALID_SOCKET) {
			closesocket(id);
		}
		--count;
		if (count == 0) {
			CleanUp();
		}
	}

	void Socket::Close()
	{
		if(id != INVALID_SOCKET){ 
			closesocket(id); 
			id = INVALID_SOCKET; 
		}
	}

}