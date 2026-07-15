#pragma once

#include <vector>
#include <thread>
#include <Client.h>
#include <optional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

using TM::Client;

class Room {
public:
	static inline uint32_t invalid_id = 0;
private:
	static inline std::vector<uint32_t> ids = {};
private:
	bool m_need_check_thread = false;
	uint32_t m_id = invalid_id;
	std::optional<uint32_t> m_passwd = std::nullopt;
	std::shared_mutex m_mtx_cs;
	std::shared_mutex m_mutex_choices;
	std::jthread m_delete_thread_thread;
	std::mutex m_mtx_threads;
	std::mutex m_mtx_need_check;
	std::vector<std::unique_ptr<Client>> m_client_sockets;
	std::vector<std::jthread> m_clients_threads;
	std::condition_variable m_cv_delete_thread;
	std::unordered_map<SOCKET, SOCKET> m_choices_of; // <socket of client(in server), client socket choice>
public:
	Room(const std::optional<uint32_t>& passwd = {});
	~Room();
	uint32_t id() { return m_id; }
	std::optional<uint32_t>& passwd() { return m_passwd; }
	std::unique_ptr<Client>& client(size_t i) { return m_client_sockets[i]; }
	std::jthread& thread(size_t i) { return m_clients_threads[i]; }
	void push_client(Client c);
private:
	static void sendMsg(Client* client, Room* room);
	static void deleteThread(Room* room);
};