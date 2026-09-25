// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "net/tcp/Server.hpp"
#include "net/tcp/Client.hpp"
#include <iostream>

namespace net {
namespace tcp {

	bool Server::Init(Ip ip, uint32_t port, std::string_view recvFrom)
	{
		skt.id = socket(static_cast<ip_t>(ip), SOCK_STREAM, IPPROTO_TCP);
		if (skt.id == invalid_socket) {
			NET_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}

		SOCKADDR_IN _port = { 0 };
		_port.sin_family = static_cast<ip_t>(ip);
		_port.sin_port = htons(port);
		_port.sin_addr.s_addr = inet_addr(recvFrom.data());
		
		int res = bind(skt.id, (SOCKADDR*)&_port, sizeof(_port));
		if (res != 0) {
			NET_SOCKET_SET_ERROR("bind() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}

		return true;
	}

	Server::Server(Ip ip, uint32_t port, std::string_view recvFrom) {
		if (!Init(ip, port, recvFrom)) {
			std::cerr << "Server init error" << std::endl;
			system("pause");
		}
	}

	bool Server::Listen(int max_clients)
	{
		if (listen(skt.id, max_clients) == SOCKET_ERROR) {
			NET_SOCKET_SET_ERROR("listen() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	std::optional<Client> Server::Accept()
	{
		socket_t _id = accept(skt.id, nullptr, nullptr);
		if (_id == invalid_socket) {
			NET_SOCKET_SET_ERROR("accept() failed with error code: " + std::to_string(WSAGetLastError()));
			return std::nullopt;
		}
		Client out_client;
		out_client.skt.id = _id;
		return out_client;
	}
}
}