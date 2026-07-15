#include "Server.h"
#include "Client.h"
#include <iostream>

namespace TM {

	bool Server::Init(Socket::Protocol protocol, Socket::Ip ip, uint32_t port, std::string_view recvFrom)
	{
		skt.id = socket(ip, protocol, 0);
		if (skt.id == INVALID_SOCKET) {
			TM_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}

		SOCKADDR_IN _port = { 0 };
		_port.sin_family = ip;
		_port.sin_port = htons(port);
		_port.sin_addr.s_addr = inet_addr(recvFrom.data());
		
		int res = bind(skt.id, (SOCKADDR*)&_port, sizeof(_port));
		if (res != 0) {
			TM_SOCKET_SET_ERROR("bind() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}

		return true;
	}

	Server::Server(Socket::Protocol protocol, Socket::Ip ip, uint32_t port, std::string_view recvFrom) {
		if (!Init(protocol, ip, port, recvFrom)) {
			std::cerr << "Server init error" << std::endl;
			system("pause");
		}
	}

	bool Server::Listen(int max_clients)
	{
		if (listen(skt.id, max_clients) == SOCKET_ERROR) {
			TM_SOCKET_SET_ERROR("listen() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	std::optional<Client> Server::Accept()
	{
		SOCKET _id = accept(skt.id, nullptr, nullptr);
		if (_id == INVALID_SOCKET) {
			TM_SOCKET_SET_ERROR("accept() failed with error code: " + std::to_string(WSAGetLastError()));
			return std::nullopt;
		}
		Client out_client;
		out_client.skt.id = _id;
		return out_client;
	}
}