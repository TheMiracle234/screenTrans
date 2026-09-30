// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_TCP_CLIENT_INL
#define NET_TCP_CLIENT_INL

#include "net/tcp/Client.hpp"
#include <iostream>
#include <cassert>

namespace net {

#ifdef __GNUC__
	template<CNumberType T>
	[[nodiscard]] inline constexpr T byteswap(T value) noexcept
	{
		if constexpr (sizeof(T) == 1)
		{
			return value;
		}
		else if constexpr (sizeof(T) == 2)
		{
			return std::bit_cast<T>(__builtin_bswap16(std::bit_cast<uint16_t>(value)));
		}
		else if constexpr (sizeof(T) == 4)
		{
			return std::bit_cast<T>(__builtin_bswap32(std::bit_cast<uint32_t>(value)));
		}
		else if constexpr (sizeof(T) == 8)
		{
			return std::bit_cast<T>(__builtin_bswap64(std::bit_cast<uint64_t>(value)));
		}
		else
		{
			static_assert(sizeof(T) <= 8, "unsupported integer size");
		}
	}
#else
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
#endif

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
		skt = Socket{ socket(static_cast<ip_t>(ip), SOCK_STREAM, IPPROTO_TCP) };
		if(skt.id() == invalid_socket) {
			NET_SOCKET_SET_ERROR("socket() failed with error code: " + std::to_string(last_socket_error()));
			return false;
		}
		return true;
	}

	inline Client::Client(socket_t skt_) :
		skt{skt_}
	{
	}

	inline Client::Client(Ip ip_version):
		skt{invalid_socket}
	{
		if (!Init(ip_version)) {
			std::cerr << "Client init error" << std::endl;
			system("pause");
			exit(1);
		}
	}

	inline bool Client::ConnectTo(Ip ip_version, const char* ip, uint16_t port)
	{		
		sockaddr_in target{};
		target.sin_family = static_cast<ip_t>(ip_version);
		target.sin_port = htons(port);
		target.sin_addr.s_addr = inet_addr(ip);

		int res = connect(skt.id(), reinterpret_cast<sockaddr*>(&target), static_cast<socklen_t>(sizeof(target)));
		if(res == socket_error) {
			NET_SOCKET_SET_ERROR("connect() failed with error code: " + std::to_string(last_socket_error()));
			return false;
		}
		return true;
	}

	inline msg_size Client::send_all(socket_t s, const void* buf, msg_size len)
	{
#		ifdef _WIN32
			using send_ret_t = int;       // send 返回值
			using send_len_t = int;       // send 第三参数
#		else
			using send_ret_t = ssize_t;
			using send_len_t = size_t;
#		endif
		assert(len >= 0);
		msg_size total = 0;
		while (total < len) {
#			ifdef __linux__
				constexpr int send_flags = MSG_NOSIGNAL;
#			else
				constexpr int send_flags = 0;
#			endif
			send_ret_t sent = send(s, static_cast<const char*>(buf) + total, static_cast<send_len_t>(len - total), send_flags);
			if (sent <= 0) {
				if (sent == 0) { return 0; }
				int code = last_socket_error();
#				ifdef _WIN32
					if (code == WSAEINTR) continue;
#				else
					if (code == EINTR) continue;
#				endif
				return static_cast<msg_size>(sent);
			}			
			total += static_cast<msg_size>(sent);
		}
		return total;
	}

	inline bool Client::Send(const void* data, size_t bytes) {
		static_assert(sizeof(size_t) >= sizeof(msg_size));
		int ret;
		//send length
		assert(std::in_range<msg_size>(bytes));
		msg_size len = static_cast<msg_size>(bytes);
		msg_size net_len = host_to_network(len);

		ret = send_all(&net_len, static_cast<msg_size>(sizeof(msg_size)));
		if (ret < 0) {
			int code = last_socket_error();
			NET_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.Close(); // server closed
			}
			return false;
		}
		else if (ret == 0) {
			skt.Close(); // server closed
			return false;
		}
		//send msg
		ret = send_all(data, len);
		if (ret < 0) {
			int code = last_socket_error();
			NET_SOCKET_SET_ERROR("send() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.Close(); // server closed
			}
			return false;
		}
		else if (ret == 0 && len > 0) {
			skt.Close(); // server closed
			return false;
		}
		return true;
	}

	inline bool Client::Send(const std::vector<uint8_t>& data)
	{
		return Send(data.data(), data.size());
	}

	inline bool Client::Send(const std::vector<int8_t>& data)
	{
		return Send(data.data(), data.size());
	}

	inline bool Client::Send(std::string_view str)
	{
		assert(std::in_range<msg_size>(str.length()));
		return Send(str.data(), str.size());
	}

	inline std::optional<msg_size> Client::recv_bytes() {
		msg_size net_len;
		msg_size ret = recv_all(&net_len, static_cast<msg_size>(sizeof(msg_size)));
		if (ret < 0) {
			int code = last_socket_error();
			NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.Close(); // server closed
			}
			return {};
		}
		else if (ret == 0) {
			skt.Close(); // server closed
			return {};
		}
		msg_size msg_len = network_to_host(net_len);
		return msg_len;
	}

	inline bool Client::recv_msg(void* data, msg_size bytes) {
		msg_size ret = recv_all(data, bytes);
		if (ret < 0) {
			int code = last_socket_error();
			NET_SOCKET_SET_ERROR("recv() failed with error code: " + std::to_string(code));
			if (Socket::CheckClosedByErrorCode(code)) {
				skt.Close(); // server closed
			}
			return false;
		}
		else if (ret == 0 && bytes > 0) {
			skt.Close(); // server closed
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

	inline msg_size Client::recv_all(socket_t s, void* buf, msg_size len) {
#		ifdef _WIN32
			using recv_ret_t = int;
			using recv_len_t = int;
#		else
			using recv_ret_t = ssize_t;
			using recv_len_t = size_t;
#		endif		
		msg_size total = 0;
		while (total < len) {
			recv_ret_t bytes = recv(s, static_cast<char*>(buf) + total, static_cast<recv_len_t>(len - total), 0);
			if (bytes <= 0) { return static_cast<msg_size>(bytes); }
			total += static_cast<msg_size>(bytes);
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
		const size_t bytes = sizeof(elem_t) * data.size();
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