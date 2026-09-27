// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_TCP_SERVER_HPP
#define NET_TCP_SERVER_HPP

#include "net/Socket.hpp"
#include <optional>

namespace net {
namespace tcp {

	class Client;
    class Server
    {
		inline static constexpr int maxBackLog = SOMAXCONN;
#		undef SOMAXCONN
		bool Init(Ip ip, uint16_t port, const char* recvFrom = "0.0.0.0");

		Socket skt;

    public:
		Server(Ip ip, uint16_t port, const char* recvFrom = "0.0.0.0");
		Server(Server& other) = delete;
		Server(Server&& other) noexcept : skt(std::move(other.skt)) {}
		bool Listen(int backLog = maxBackLog);
		socket_t Id() { return skt.id(); }
		void Close() { skt.Close(); }
		bool Closed() { return skt.Closed(); }
		std::optional<Client> Accept();
    };
}
}
#endif