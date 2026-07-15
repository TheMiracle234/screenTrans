#ifndef TMCLIENT_H
#define TMCLIENT_H

#include "Socket.h"
#include "Server.h"
#include <optional>
#include <vector>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <bit>
#include <algorithm>
#include <execution>

namespace TM {

    template<typename T>
    requires std::is_integral_v<T> || std::is_floating_point_v<T>
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

    template<typename T>
    requires std::is_integral_v<T> || std::is_floating_point_v<T>
    inline constexpr T host_to_network(T value) noexcept
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

    template<typename T>
    requires std::is_integral_v<T> || std::is_floating_point_v<T>
    inline constexpr T network_to_host(T value) noexcept
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
	class TM_SOCKET_API Client
	{
        friend class Server;
	private:
		Socket::Ip ip_version;
        Socket skt;

		bool Init(Socket::Protocol protocol, Socket::Ip ip);
		msg_size send_all(SOCKET s, const char* buf, msg_size len);
		msg_size recv_all(SOCKET s, char* buf, msg_size len);
		Client() = default;

        static bool stringViewEndOk(std::string_view str) { return !str.empty() && str[str.size()] == '\0'; }

	public:
        Client(Socket::Protocol protocol, Socket::Ip ip);
        Client(const Client& other) = delete;
        Client(Client&& other) noexcept : skt(std::move(other.skt)), ip_version(other.ip_version) {}
		bool ConnectTo(const std::string& ip, uint32_t port);
		SOCKET Id() { return skt.id; }
        void Close() { skt.Close(); }
        bool Closed() { return skt.Closed(); }

		bool Send(const std::vector<uint8_t>& data);

        // when using this, there is no ntoh or hton
        bool Send(const void* data, msg_size len);

        //auto hton, so data inside will be changed, so copy or move
        template<typename vec>
        requires is_vector_v<vec>
        bool Send(vec data) {
            using elem_t = typename vec::value_type;
            std::for_each(data.begin(), data.end(), [](elem_t& elem) { elem = host_to_network(elem); });
            return Send(data.data(), (msg_size)(sizeof(elem_t) * data.size()));
        }

		template<typename T>
        requires std::is_integral_v<T> || std::is_floating_point_v<T>
        bool Send(const T data) {
            T net_data = host_to_network(data);
			return Send(std::vector<uint8_t>(
				reinterpret_cast<const uint8_t*>(&net_data), reinterpret_cast<const uint8_t*>(&net_data) + sizeof(T)
			));
		}

		bool Send(const std::string& str);

		std::optional<std::vector<uint8_t>> Receive();
        
        template<typename T>
        requires std::is_integral_v<T> || std::is_floating_point_v<T>
        std::optional<T> ReceiveParseTo() {
            auto data = this->Receive();
            if (!data || data->size() != sizeof(T)) {
                TM_SOCKET_SET_ERROR("receive not match");
                return {};
            }
            T buf;
            memcpy(&buf, data->data(), sizeof(T));
			return network_to_host(buf);
		}

        // auto ntoh
        template<typename T>
        requires std::is_integral_v<T> || std::is_floating_point_v<T>
        std::optional<std::vector<T>> ReceiveVec() {
            auto bytes = Receive();
            if (!bytes || bytes->size() % sizeof(T) != 0) {
                TM_SOCKET_SET_ERROR("ReceiveVec but elem not match size");
                return {};
            }
            std::vector<T> res(bytes->size() / sizeof(T));
            memcpy(res.data(), bytes->data(), bytes->size());
            std::for_each(res.begin(), res.end(), [](T& elem) { elem = network_to_host(elem); });
            return res;
        }

		// make sure the msg send to you is a string(end by \0)
		std::optional<std::string> ReceiveString();
	};
};

#endif