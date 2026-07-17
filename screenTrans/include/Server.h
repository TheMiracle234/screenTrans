#ifndef TMSERVER_H
#define TMSERVER_H

#include "Socket.h"
#include <optional>

namespace TM {

	class Client;
    class TM_SOCKET_API Server
    {
		static constexpr int MAX_CLIENTS = 0x7FFFFFFF;
        bool Init(Socket::Protocol protocol, Socket::Ip ip, uint32_t port, std::string_view recvFrom = "0.0.0.0");

		Socket skt;

    public:
		Server(Socket::Protocol protocol, Socket::Ip ip, uint32_t port, std::string_view recvFrom = "0.0.0.0");
		Server(Server& other) = delete;
		Server(Server&& other) noexcept : skt(std::move(other.skt)) {}
		bool Listen(int max_clients = MAX_CLIENTS);
		SOCKET Id() { return skt.id; }
		void Close() { skt.Close(); }
		bool Closed() { return skt.Closed(); }
		std::optional<Client> Accept();
    };

}

#endif