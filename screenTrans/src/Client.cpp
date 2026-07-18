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

#include "Client.h"
#include <iostream>
#include <cassert>

namespace TM {

	bool Client::Init(Socket::Protocol protocol, Socket::Ip ip)
	{
		skt.id = socket(ip, protocol, 0);
		ip_version = ip;
		if(skt.id == INVALID_SOCKET) {
			TM_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	Client::Client(Socket::Protocol protocol, Socket::Ip ip){
		if (!Init(protocol, ip)) {
			std::cerr << "Client init error" << std::endl;
			system("pause");
			exit(1);
		}
	}

	bool Client::ConnectTo(const std::string& ip, uint32_t port)
	{		
		SOCKADDR_IN target;
		target.sin_family = this->ip_version;
		target.sin_port = htons(port);
		target.sin_addr.s_addr = inet_addr(ip.data());

		int res = connect(skt.id, (SOCKADDR*)&target, sizeof(target));
		if(res == SOCKET_ERROR) {
			TM_SOCKET_SET_ERROR("connect() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	msg_size Client::send_all(SOCKET s, const char* buf, msg_size len)
	{
		msg_size total = 0;
		while (total < len) {
			int sent = send(s, buf + total, static_cast<int>(len - total), 0
			);
			if (sent <= 0)
				return sent;
			total += sent;
		}
		return (int)total;
	}

	bool Client::Send(const std::vector<uint8_t>& data)
	{
		int ret;
		//send length
		msg_size len = (msg_size)data.size();
		msg_size net_len = host_to_network(len);

		ret = send_all(skt.id, reinterpret_cast<char*>(&net_len), sizeof(msg_size));
		if (ret < 0) {
			int code = WSAGetLastError();
			TM_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = INVALID_SOCKET; // server closed
			}
			return false;
		}
		else if (ret == 0) {
			skt.id = INVALID_SOCKET; // server closed
			return false;
		}
		//send msg
		ret = send_all(skt.id, reinterpret_cast<const char*>(data.data()), len);
		if (ret < 0) {
			int code = WSAGetLastError();
			TM_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = INVALID_SOCKET; // server closed
			}
			return false;
		}
		else if (ret == 0 && len > 0) {
			skt.id = INVALID_SOCKET; // server closed
			return false;
		}
		return true;
	}

	bool Client::Send(const void* data, msg_size len) {
		return Send(std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data), reinterpret_cast<const uint8_t*>(data) + len));
	}

	bool Client::Send(const std::string& str)
	{
		return this->Send(
			std::vector<uint8_t>(
				reinterpret_cast<const uint8_t*>(str.data()), reinterpret_cast<const uint8_t*>( str.data() + str.length() + 1 )
			)
		);
	}

	std::optional<std::vector<uint8_t>> Client::Receive() {
		msg_size net_len;
		int ret = recv_all(skt.id, reinterpret_cast<char*>(&net_len), sizeof(msg_size));
		if (ret < 0) {
			int code = WSAGetLastError();
			TM_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = INVALID_SOCKET; // server closed
			}
			return {};
		}
		else if (ret == 0) {
			skt.id = INVALID_SOCKET; // server closed
			return {};
		}
		msg_size msg_len = network_to_host(net_len);
		if (msg_len == 0) {
			return std::vector<uint8_t>();
		}

		std::vector<uint8_t> data(msg_len);
		ret = recv_all(skt.id, reinterpret_cast<char*>(data.data()), msg_len);
		if (ret < 0) {
			int code = WSAGetLastError();
			TM_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = INVALID_SOCKET; // server closed
			}
			return {};
		}
		else if (ret == 0 && msg_len > 0) {
			skt.id = INVALID_SOCKET; // server closed
			return {};
		}
		return data;
	}

	msg_size Client::recv_all(SOCKET s, char* buf, msg_size len) {
		msg_size total = 0;
		while (total < len) {
			int bytes = recv(s, buf + total, static_cast<int>(len - total), 0);
			if (bytes <= 0) return bytes;
			total += bytes;
		}
		return total;
	}

	std::optional<std::string> Client::ReceiveString(){
		auto str = Receive();
		if (!str) {
			return {};
		}
		if (!str->empty() && str->back() != '\0') {
			TM_SOCKET_SET_ERROR("ReceiveString not end by \'\\0\'");
			return {};
		}
		return std::string(reinterpret_cast<char*>(str->data()));
	}
}