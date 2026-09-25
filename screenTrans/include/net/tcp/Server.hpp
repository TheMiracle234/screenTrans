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
		static constexpr int MAX_CLIENTS = 0x7FFFFFFF;
        bool Init(Ip ip, uint32_t port, std::string_view recvFrom = "0.0.0.0");

		Socket skt;

    public:
		Server(Ip ip, uint32_t port, std::string_view recvFrom = "0.0.0.0");
		Server(Server& other) = delete;
		Server(Server&& other) noexcept : skt(std::move(other.skt)) {}
		bool Listen(int max_clients = MAX_CLIENTS);
		socket_t Id() { return skt.id; }
		void Close() { skt.Close(); }
		bool Closed() { return skt.Closed(); }
		std::optional<Client> Accept();
    };
}
}
#endif