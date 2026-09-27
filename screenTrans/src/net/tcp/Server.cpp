// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "net/tcp/Server.hpp"
#include "net/tcp/Client.hpp"
#include <iostream>
#include <cassert>

namespace net {
namespace tcp {

	bool Server::Init(Ip ip, uint16_t port, const char* recvFrom)
	{
		skt = Socket{ socket(static_cast<ip_t>(ip), SOCK_STREAM, IPPROTO_TCP) };
		if (skt.id() == invalid_socket) {
			NET_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(last_socket_error()));
			return false;
		}

		sockaddr_in _port{};
		_port.sin_family = static_cast<ip_t>(ip);
		_port.sin_port = htons(port);
		_port.sin_addr.s_addr = inet_addr(recvFrom);
		
		int res = bind(skt.id(), reinterpret_cast<sockaddr*>(&_port), static_cast<socklen_t>(sizeof(_port)));
		if (res != 0) {
			NET_SOCKET_SET_ERROR("bind() failed with error code: " + std::to_string(last_socket_error()));
			return false;
		}

		return true;
	}

	Server::Server(Ip ip, uint16_t port, const char* recvFrom) :
		skt{invalid_socket}
	{
		if (!Init(ip, port, recvFrom)) {
			std::cerr << "Server init error" << std::endl;
			(void)getchar();
		}
	}

	bool Server::Listen(int backLog)
	{
		assert(backLog <= maxBackLog);
		if (listen(skt.id(), backLog) == socket_error) {
			NET_SOCKET_SET_ERROR("listen() failed with error code: " + std::to_string(last_socket_error()));
			return false;
		}
		return true;
	}

	std::optional<Client> Server::Accept()
	{
		socket_t id;
		for (;;) {
			id = accept(skt.id(), nullptr, nullptr);
			if (id != invalid_socket) break;
			int code = last_socket_error();
#			ifdef _WIN32
				if (code == WSAEINTR) continue;
#			else
				if (code == EINTR) continue;
#			endif
			NET_SOCKET_SET_ERROR("accept() failed with error code: "
				+ std::to_string(code));
			return std::nullopt;
		}
		return Client{ id };
	}
}
}