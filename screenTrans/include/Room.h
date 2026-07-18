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

#pragma once

#include <vector>
#include <thread>
#include <Client.h>
#include <optional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <semaphore>

using TM::Client;

class Room {
public:
	constexpr static inline uint32_t invalid_id = 0;
	constexpr static inline uint32_t invalid_passwd = 0;
private:
	static inline std::mutex mtx_ids;
	static inline std::vector<uint32_t> ids = {};
private:
	using Flag = uint8_t;
	enum : Flag{
		f_none = 0,
		f_used = (Flag)1 << 0, // used to have at least one client
	};
	Flag m_flag = f_none;
	uint32_t m_id = invalid_id;
	std::optional<uint32_t> m_passwd = std::nullopt;
	std::counting_semaphore<> m_cs_dead_threads{0};
	std::jthread m_delete_thread_thread;
	std::shared_mutex m_mtx_cs;
	std::shared_mutex m_mutex_choices;
	std::vector<std::unique_ptr<Client>> m_client_sockets;
	std::vector<std::jthread> m_clients_threads;
	std::mutex m_mtx_id;
	std::mutex m_mtx_threads;
	std::mutex m_mtx_used;
	std::mutex m_mtx_need_check;
	std::unordered_map<SOCKET, SOCKET> m_choices_of; // <socket of client(in server), client socket choice>
public:
	Room(const std::optional<uint32_t>& passwd = {});
	~Room();
	Room(const Room&) = delete;
	Room(Room&&) noexcept = delete;
	Room& operator=(const Room&) = delete;
	Room& operator=(Room&&) noexcept = delete;
	bool used() { std::lock_guard lock(m_mtx_used); return m_flag & f_used; }
	bool empty() { std::lock_guard lock(m_mtx_cs); return m_client_sockets.size() == 0; }
	uint32_t id() { std::lock_guard lock(m_mtx_id); return m_id; }
	std::optional<uint32_t>& passwd() { return m_passwd; }
	std::unique_ptr<Client>& client(size_t i) { return m_client_sockets[i]; }
	std::jthread& thread(size_t i) { return m_clients_threads[i]; }
	void pushClient(Client c);
private:
	static void sendMsg(Client* client, Room* room);
	static void deleteThread(Room* room);
};