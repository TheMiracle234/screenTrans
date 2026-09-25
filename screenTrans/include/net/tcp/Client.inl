// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_TCP_CLIENT_INL
#define NET_TCP_CLIENT_INL

#include "net/tcp/Client.hpp"
#include <iostream>
#include <cassert>

namespace net {

	template<CNumberType T>
	[[nodiscard]] inline constexpr T byteswap(T value) noexcept
	{
		if constexpr (sizeof(T) == 1)
		{
			return value;
		}
		else if constexpr (sizeof(T) == 2)
		{
			return std::bit_cast<T>(_byteswap_ushort(std::bit_cast<uint16_t>(value)));
		}
		else if constexpr (sizeof(T) == 4)
		{
			return std::bit_cast<T>(_byteswap_ulong(std::bit_cast<uint32_t>(value)));
		}
		else if constexpr (sizeof(T) == 8)
		{
			return std::bit_cast<T>(_byteswap_uint64(std::bit_cast<uint64_t>(value)));
		}
		else
		{
			static_assert(sizeof(T) <= 8, "unsupported integer size");
		}
	}

	///////////////////////////////////////////////////////////
	// host <-> network
	///////////////////////////////////////////////////////////

	template<CNumberType T>
	[[nodiscard]] inline constexpr T host_to_network(T value) noexcept
	{
		if constexpr (std::endian::native == std::endian::little)
		{
			return byteswap(value);
		}
		else
		{
			return value;
		}
	}

	template<CNumberType T>
	[[nodiscard]] inline constexpr T network_to_host(T value) noexcept
	{
		if constexpr (std::endian::native == std::endian::little)
		{
			return byteswap(value);
		}
		else
		{
			return value;
		}
	}


namespace tcp {

	inline bool Client::Init(Ip ip)
	{
		skt.id = socket(static_cast<ip_t>(ip), SOCK_STREAM, IPPROTO_TCP);
		ip_version = ip;
		if(skt.id == invalid_socket) {
			NET_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	inline Client::Client(Ip ip){
		if (!Init(ip)) {
			std::cerr << "Client init error" << std::endl;
			system("pause");
			exit(1);
		}
	}

	inline bool Client::ConnectTo(const char* ip, uint32_t port)
	{		
		SOCKADDR_IN target;
		target.sin_family = static_cast<ip_t>(this->ip_version);
		target.sin_port = htons(port);
		target.sin_addr.s_addr = inet_addr(ip);

		int res = connect(skt.id, (SOCKADDR*)&target, sizeof(target));
		if(res == SOCKET_ERROR) {
			NET_SOCKET_SET_ERROR("connect() failed with error code: " + std::to_string(WSAGetLastError()));
			return false;
		}
		return true;
	}

	inline msg_size Client::send_all(socket_t s, const char* buf, msg_size len)
	{
		assert(len >= 0);
		msg_size total = 0;
		while (total < len) {
			int sent = send(s, buf + total, static_cast<int>(len - total), 0);
			if (sent <= 0)
				return sent;
			total += sent;
		}
		return (int)total;
	}

	inline bool Client::Send(const void* data, int64_t bytes) {
		static_assert(sizeof(int64_t) >= sizeof(msg_size));
		assert(bytes >= 0);
		int ret;
		//send length
		assert(std::in_range<msg_size>(bytes));
		msg_size len = static_cast<msg_size>(bytes);
		msg_size net_len = host_to_network(len);

		ret = send_all(skt.id, reinterpret_cast<char*>(&net_len), sizeof(msg_size));
		if (ret < 0) {
			int code = WSAGetLastError();
			NET_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = invalid_socket; // server closed
			}
			return false;
		}
		else if (ret == 0) {
			skt.id = invalid_socket; // server closed
			return false;
		}
		//send msg
		ret = send_all(skt.id, reinterpret_cast<const char*>(data), len);
		if (ret < 0) {
			int code = WSAGetLastError();
			NET_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = invalid_socket; // server closed
			}
			return false;
		}
		else if (ret == 0 && len > 0) {
			skt.id = invalid_socket; // server closed
			return false;
		}
		return true;
	}

	inline bool Client::Send(const std::vector<uint8_t>& data)
	{
		return Send(data.data(), static_cast<int64_t>(data.size()));
	}

	inline bool Client::Send(const std::vector<int8_t>& data)
	{
		return Send(data.data(), static_cast<int64_t>(data.size()));
	}

	inline bool Client::Send(std::string_view str)
	{
		assert(std::in_range<msg_size>(str.length()));
		return Send(str.data(), str.size());
	}

	inline std::optional<msg_size> Client::recv_bytes() {
		msg_size net_len;
		msg_size ret = recv_all(skt.id, reinterpret_cast<char*>(&net_len), sizeof(msg_size));
		if (ret < 0) {
			int code = WSAGetLastError();
			NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = invalid_socket; // server closed
			}
			return {};
		}
		else if (ret == 0) {
			skt.id = invalid_socket; // server closed
			return {};
		}
		msg_size msg_len = network_to_host(net_len);
		return msg_len;
	}

	inline bool Client::recv_msg(void* data, msg_size bytes) {
		msg_size ret = recv_all(skt.id, reinterpret_cast<char*>(data), bytes);
		if (ret < 0) {
			int code = WSAGetLastError();
			NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.id = invalid_socket; // server closed
			}
			return false;
		}
		else if (ret == 0 && bytes > 0) {
			skt.id = invalid_socket; // server closed
			return false;
		}
		return true;
	}

	template<CNumberType T>
	inline bool Client::ReceiveBy(T& out) {
		auto o_msg_bytes = recv_bytes();
		if (!o_msg_bytes) { return false; }
		msg_size msg_bytes = *o_msg_bytes;
		if(sizeof(T) != msg_bytes) [[unlikely]] {
			NET_SOCKET_SET_ERROR("receive not match");
			return false;
		}
		if (!recv_msg(&out, msg_bytes)) {
			return false;
		}
		out = network_to_host(out);
		return true;
	}

	inline bool Client::ReceiveBy(std::string& out) {
		auto o_msg_bytes = recv_bytes();
		if (!o_msg_bytes) { return false; }
		msg_size msg_bytes = *o_msg_bytes;
		out.resize(msg_bytes);
		if (!recv_msg(out.data(), msg_bytes)) {
			return false;
		}
		return true;
	}

	template<CIsVector Vec>
	inline bool Client::ReceiveBy(Vec& out) {
		bool res{ true };
		auto o_msg_bytes = recv_bytes();
		if (!o_msg_bytes) { return false; }
		msg_size msg_bytes = *o_msg_bytes;
		using elem_t = typename Vec::value_type;
		static_assert(!std::same_as<elem_t, bool>);
		if (msg_bytes % sizeof(elem_t) != 0) [[unlikely]] {
			NET_SOCKET_SET_ERROR("Receive vector but elem not match size");
			res = false;
		}
		out.resize(msg_bytes / sizeof(elem_t));
		if (!recv_msg(out.data(), msg_bytes)) {
			return false;
		}
		if constexpr (sizeof(elem_t) > 1) {
			std::ranges::for_each(out, [](elem_t& e) { e = network_to_host(e); });
		}
		return res;
	}


	//inline std::optional<std::vector<uint8_t>> Client::Receive() {
	//	msg_size net_len;
	//	msg_size ret = recv_all(skt.id, reinterpret_cast<char*>(&net_len), sizeof(msg_size));
	//	if (ret < 0) {
	//		int code = WSAGetLastError();
	//		NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
	//		if (Socket::CheckClosedByErrorCode(code)) {
	//			skt.id = invalid_socket; // server closed
	//		}
	//		return {};
	//	}
	//	else if (ret == 0) {
	//		skt.id = invalid_socket; // server closed
	//		return {};
	//	}
	//	msg_size msg_len = network_to_host(net_len);
	//	if (msg_len == 0) {
	//		return std::vector<uint8_t>();
	//	}

	//	std::vector<uint8_t> data(msg_len);
	//	ret = recv_all(skt.id, reinterpret_cast<char*>(data.data()), msg_len);
	//	if (ret < 0) {
	//		int code = WSAGetLastError();
	//		NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
	//		if (Socket::CheckClosedByErrorCode(code)) {
	//			skt.id = invalid_socket; // server closed
	//		}
	//		return {};
	//	}
	//	else if (ret == 0 && msg_len > 0) {
	//		skt.id = invalid_socket; // server closed
	//		return {};
	//	}
	//	return data;
	//}

	//template<CNumberType T>
	//inline [[nodiscard]] std::optional<T> Client::ReceiveParseTo() {
	//	auto data = this->Receive();
	//	if (!data || data->size() != sizeof(T)) {
	//		NET_SOCKET_SET_ERROR("receive not match");
	//		return {};
	//	}
	//	T buf;
	//	memcpy(&buf, data->data(), sizeof(T));
	//	return network_to_host(buf);
	//}

	//// auto ntoh
	//template<CNumberType T>
	//inline [[nodiscard]] std::optional<std::vector<T>> Client::ReceiveVec() {
	//	auto bytes = Receive();
	//	if (!bytes || bytes->size() % sizeof(T) != 0) {
	//		NET_SOCKET_SET_ERROR("ReceiveVec but elem not match size");
	//		return {};
	//	}
	//	std::vector<T> res(bytes->size() / sizeof(T));
	//	memcpy(res.data(), bytes->data(), bytes->size());
	//	std::for_each(res.begin(), res.end(), [](T& elem) { elem = network_to_host(elem); });
	//	return res;
	//}
	//inline std::optional<std::string> Client::ReceiveString() {
	//	auto str = Receive();
	//	if (!str) {
	//		return {};
	//	}
	//	return std::string(reinterpret_cast<const char*>(str->data()), str->size());
	//}

	inline msg_size Client::recv_all(socket_t s, char* buf, msg_size len) {
		msg_size total = 0;
		while (total < len) {
			int bytes = recv(s, buf + total, static_cast<int>(len - total), 0);
			if (bytes <= 0) return bytes;
			total += bytes;
		}
		return total;
	}


	//auto hton, so data inside will be changed, so copy or move
	template<typename vec>
		requires is_vector_v<vec>&& CNumberType<typename vec::value_type>
	inline bool Client::Send(vec data) {
		using elem_t = typename vec::value_type;
		static_assert(!std::same_as<elem_t, bool>);
		std::for_each(data.begin(), data.end(), [](elem_t& elem) { elem = host_to_network(elem); });
		const int64_t bytes = static_cast<int64_t>(sizeof(elem_t) * data.size());
		assert(std::in_range<msg_size>(bytes));
		return Send(data.data(), bytes);
	}

	template<CNumberType T>
	inline bool Client::Send(const T data) {
		T net_data = host_to_network(data);
		return Send(&net_data, sizeof(T));
	}

}
}

#endif