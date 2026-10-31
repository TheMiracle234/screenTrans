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
			bool Init(Ip ip, uint16_t port, const char* recvFrom = nullptr);

			Socket skt;

		public:
			// ip     : Ip::v4 / Ip::v6 / Ip::Auto
			//          Ip::Auto (= AF_UNSPEC) 时由 getaddrinfo 返回候选地址，
			//          按顺序尝试 bind，第一个成功的 result 成为监听 socket；
			//          通常配合 recvFrom=nullptr 得到双栈监听。
			// recvFrom: 要 bind 的本地地址；nullptr 表示"本机所有接口"（配合 AI_PASSIVE）。
			//          非 nullptr 时必须与 ip 的 family 兼容
			//          （例如 ip=Ip::v6 时不能传 IPv4 字面量）。
			Server(Ip ip, uint16_t port, const char* recvFrom = nullptr);
			Server(const Server& other) = delete;
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