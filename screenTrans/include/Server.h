/*
    screenTrans - online meeting app
    Copyright (C) 2026 Yuan Aowei

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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