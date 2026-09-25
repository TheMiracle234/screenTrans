// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#ifndef NET_TCP_CLIENT_HPP
#define NET_TCP_CLIENT_HPP

#include "net/Socket.hpp"
#include "net/tcp/Server.hpp"
#include <optional>
#include <vector>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <bit>
#include <algorithm>
#include <mutex>
#include <utility>
#include <cassert>

namespace net {

    // enum is not included, because 
    template<typename T>
    concept CNumberType = std::is_integral_v<T> || std::is_floating_point_v<T>;

    template<CNumberType T>
    [[nodiscard]] inline constexpr T byteswap(T value) noexcept;

    template<CNumberType T>
    [[nodiscard]] inline constexpr T host_to_network(T value) noexcept;

    template<CNumberType T>
    [[nodiscard]] inline constexpr T network_to_host(T value) noexcept;

    // 主模板：不是 vector
    template <typename T>
    struct is_vector : std::false_type {};

    // 特化匹配任何 vector<...>
    template <typename T, typename Alloc>
    struct is_vector<std::vector<T, Alloc>> : std::true_type {};

    // 辅助变量模板 (C++17)
    template <typename T>
    inline constexpr bool is_vector_v = is_vector<T>::value;

	using msg_size = int;

    namespace tcp {

        class Client
        {
            friend class Server;
        private:
            Ip ip_version;
            Socket skt;

            bool Init(Ip ip);
            msg_size send_all(socket_t s, const char* buf, msg_size len);
            msg_size recv_all(socket_t s, char* buf, msg_size len);
            Client() = default;

        public:
            Client(Ip ip);
            Client(const Client& other) = delete;
            Client(Client&& other) noexcept : skt(std::move(other.skt)), ip_version(other.ip_version) {}
            void operator=(Client&& other) noexcept { skt = std::move(other.skt); ip_version = other.ip_version; }
            bool ConnectTo(const char* ip, uint32_t port);
            socket_t Id() { return skt.id; }

            void Close() { skt.Close(); }
            bool Closed() { return skt.Closed(); }

            // when using this, there is no ntoh or hton
            // bases of all Send
            bool Send(const void* data, int64_t bytes);
            bool Send(const std::vector<uint8_t>& data);
            bool Send(const std::vector<int8_t>& data);

            //auto hton, so data inside will be changed, so copy or move
            template<typename vec>
                requires is_vector_v<vec>&& CNumberType<typename vec::value_type>
            bool Send(vec data);

            template<CNumberType T>
            bool Send(const T data);

            bool Send(std::string_view str);

            [[nodiscard]] std::optional<std::vector<uint8_t>> Receive();

            template<CNumberType T>
            [[nodiscard]] std::optional<T> ReceiveParseTo();

            // auto ntoh
            template<CNumberType T>
            [[nodiscard]] std::optional<std::vector<T>> ReceiveVec();

            // make sure the msg sent to you is a string(end by \0)
            [[nodiscard]] std::optional<std::string> ReceiveString();
        };
    }
};

#include <net/tcp/Client.inl>
#endif