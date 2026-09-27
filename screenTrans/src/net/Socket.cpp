// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "net/Socket.hpp"
#include <iostream>
#include <cassert>

namespace net {
	Socket::Socket(socket_t id) :
		m_id(id)
	{ 
		assert(started == true);
	}

	Socket::Socket(Socket&& other) noexcept {
		if (&other == this) { return; }
		Close();
		m_id = other.m_id;
		other.m_id = invalid_socket;
	}

	Socket& Socket::operator=(Socket&& other) noexcept {
		if (&other == this) { return *this; }
		Close();
		m_id = other.m_id;
		other.m_id = invalid_socket;
		return *this;
	}

	void Socket::SetError(std::string_view str, std::string_view file, int line) {
		last_err = std::string(file) + "\nline " + std::to_string(line) + ": " + std::string(str) + "\n";
#		ifndef NDEBUG
			std::cerr << last_err << std::endl;
#		endif	
	}

	bool Socket::StartUp()
	{
#		ifdef _WIN32
			WSADATA wsaData;
			int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (result != 0) {
				NET_SOCKET_SET_ERROR("WSAStartup failed with error code: " + std::to_string(result));
				return false;
			}
#		endif
#		ifndef NDEBUG
			started = true;
#		endif
		return true;
	}

	void Socket::CleanUp()
	{
#ifdef _WIN32
		WSACleanup();
#endif
	}

	bool Socket::CheckClosedByErrorCode(int code)
	{
		switch (code) {
#ifdef _WIN32
			// ---- Windows ----
		case WSAECONNRESET:      // 对端重置连接
		case WSAENOTCONN:        // 套接字未连接
		case WSAETIMEDOUT:       // 连接超时
		case WSAECONNABORTED:    // 本地中止
		case WSAESHUTDOWN:       // 套接字已关闭
		case WSAENETDOWN:        // 网络子系统失效
		case WSAENETRESET:       // 网络连接被重置
		case WSAECONNREFUSED:    // 连接被拒绝
		case WSAENETUNREACH:     // 网络不可达
		case WSAEHOSTUNREACH:    // 主机不可达
		case WSAENOBUFS:         // 缓冲区不足
		case WSAEOPNOTSUPP:      // 操作不支持
		case WSAEDISCON:         // 对端正常关闭 (消息协议)
		case WSAENOTSOCK:        // 无效套接字
		case WSAEHOSTDOWN:       // 目标主机已关闭
			return true;
#else
			// ---- Linux / POSIX ----
		case ECONNRESET:         // 对端重置连接
		case ENOTCONN:           // 套接字未连接
		case ETIMEDOUT:          // 连接超时
		case ECONNABORTED:       // 本地中止
		case ESHUTDOWN:          // 套接字已关闭
		case ENETDOWN:           // 网络子系统失效
		case ENETRESET:          // 网络连接被重置
		case ECONNREFUSED:       // 连接被拒绝
		case ENETUNREACH:        // 网络不可达
		case EHOSTUNREACH:       // 主机不可达
		case ENOBUFS:            // 缓冲区不足
		case EOPNOTSUPP:         // 操作不支持
		case ENOTSOCK:           // 无效套接字
		case EHOSTDOWN:          // 目标主机已关闭
			return true;
#endif
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
		Close();
	}

	void Socket::Close()
	{
		if(m_id != invalid_socket){
#ifdef _WIN32
			::closesocket(m_id);
#else
			::close(m_id);
#endif		
		}
		m_id = invalid_socket;
	}

}