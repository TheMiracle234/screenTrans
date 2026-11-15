// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <vector>
#include <thread>
#include <optional>
#include <memory>
#include <mutex>
#include <shared_mutex>
//#include <unordered_map>
#include <semaphore>
#include <utility>

#include <net/tcp/Client.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

namespace user {
	using id_t = std::uint64_t;
	inline constexpr id_t invalid_id = static_cast<id_t>(net::invalid_socket);
	inline constexpr user::id_t public_target = invalid_id;
}

namespace room {
	constexpr static inline uint32_t invalid_id = 0;
}

class Room {
	template<class A, class B>
	using UnorderedMap = boost::unordered_flat_map<A, B>;
	template<class A>
	using UnorderedSet = boost::unordered_flat_set<A>;

public:
	constexpr static inline uint32_t invalid_passwd = 0;
private:
	static inline std::mutex mtx_ids;
	static inline std::vector<uint32_t> ids = {};
private:
	//using Flag = uint8_t;
	//enum : Flag{
	//	f_none = 0,
	//	f_used = (Flag)1 << 0, // used to have at least one client
	//};
	//Flag m_flag = f_none;
	std::atomic<uint32_t> m_id = room::invalid_id;
	std::atomic<net::socket_t> m_host_skt{ net::invalid_socket };
	std::atomic<uint32_t> m_passwd{ invalid_passwd };
	std::atomic<bool> m_used{ false };
	std::counting_semaphore<> m_cs_dead_threads{0};
	std::jthread m_delete_thread_thread;
	std::shared_mutex m_mtx_client_sockets;
	std::shared_mutex m_mutex_choices;
	struct User { 
		user::id_t id;
		net::tcp::Client client;
		net::tcp::Client client_audio;
		std::mutex mtx_client;
		std::mutex mtx_audio;
	};
	std::vector<std::unique_ptr<User>> m_client_sockets;
	std::vector<std::jthread> m_clients_threads;
	std::mutex m_mtx_used;
	std::mutex m_mtx_need_check;
	UnorderedMap<net::socket_t, UnorderedSet<net::socket_t>> m_choices_of; // <socket of client(in server), client socket choice>
	//std::unordered_map<net::socket_t, net::socket_t> m_choices_of; // <socket of client(in server), client socket choice>
public:
	Room(uint32_t passwd);
	~Room();
	Room(const Room&) = delete;
	Room(Room&&) noexcept = delete;
	Room& operator=(const Room&) = delete;
	Room& operator=(Room&&) noexcept = delete;
	//bool used() { std::lock_guard lock(m_mtx_used); return m_flag & f_used; }
	bool used() { return m_used.load(std::memory_order_acquire); }
	bool empty() { std::lock_guard lock(m_mtx_client_sockets); return m_client_sockets.size() == 0; }
	uint32_t id() { return m_id.load(std::memory_order_acquire); }
	uint32_t passwd() { return m_passwd.load(std::memory_order_acquire); }	
	void setPasswd(uint32_t passwd) { m_passwd.store(passwd, std::memory_order_release); }
	//std::unique_ptr<Client>& client(size_t i) { return m_client_sockets[i]; }
	std::jthread& thread(size_t i) { return m_clients_threads[i]; }
	void pushClient(net::tcp::Client c, net::tcp::Client c_audio);
	void setHost(user::id_t id) { m_host_skt = getSocketById(id); }

	constexpr static user::id_t makeId(net::socket_t skt) { return static_cast<user::id_t>(skt); }
	constexpr static net::socket_t getSocketById(user::id_t id) { return static_cast<net::socket_t>(id); }
private:
	void threadSendMsg(User* cm);
	void threadSendAudio(User* clientMtx, std::atomic<bool>& closedSignalSent);
	void threadDeleteThread();
};