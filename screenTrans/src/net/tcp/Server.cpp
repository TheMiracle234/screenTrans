// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "net/tcp/Server.hpp"
#include "net/tcp/Client.hpp"
#include "net/AddrInfo.hpp"
#include <iostream>
#include <cassert>
#include <utility>

namespace net {
namespace tcp {

	bool Server::Init(Ip ip, uint16_t port, const char* recvFrom)
	{
		auto addrs = AddrInfo::Resolve(
			recvFrom,
			port,
			static_cast<ip_t>(ip),
			SOCK_STREAM,
			IPPROTO_TCP,
			recvFrom ? 0 : AI_PASSIVE);

		if (!addrs) {
			NET_SOCKET_SET_ERROR("getaddrinfo() failed: " + AddrInfo::getLastError());
			return false;
		}

		int last_code = 0;
		for (const addrinfo& ai : *addrs) {
			Socket tmp{ socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol) };
			if (tmp.id() == invalid_socket) {
				last_code = last_socket_error();
				continue;
			}

			// 双栈：无论 Windows 默认 1 还是 Linux 默认 0，统一强制关掉 V6ONLY。
			// 失败仅意味着退回平台默认，不当作 Init 失败。
			if (ai.ai_family == AF_INET6) {
				int off = 0;
				(void)setsockopt(tmp.id(), IPPROTO_IPV6, IPV6_V6ONLY,
					reinterpret_cast<const char*>(&off),
					static_cast<socklen_t>(sizeof(off)));
			}

			if (bind(tmp.id(), ai.ai_addr, static_cast<socklen_t>(ai.ai_addrlen)) == 0) {
				skt = std::move(tmp);
				return true;
			}
			last_code = last_socket_error();
		}

		NET_SOCKET_SET_ERROR("bind() failed for all addrinfo results, last error code: "
			+ std::to_string(last_code));
		return false;
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