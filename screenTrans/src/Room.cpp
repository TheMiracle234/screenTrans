#include <Room.h>
#include <iostream>
#include <st_signals.h>
#include <cassert>
#include <random>

#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
//#define pv(x) std::cout<< #x << ": " << x << "\n"

/*
recv: id, name, choose_socket, audio_frames, pk_size, pk[n]
send: signals, id (, name (, audio_frames, pk_size, pk[n]))

if closed
	send: signals, id

if choiceNotMatch
	send: signals, id, name

*/
void Room::sendMsg(Client* client, Room* room) {
	SOCKET save_id = INVALID_SOCKET;

	using Flag0 = uint8_t;
	enum : Flag0 {
		flag_closedSignalSent = 0x01,
		flag_clientChoiceRecorded = 0x02,
	};
	Flag0 flags = 0;
	SOCKET last_chosen_socket = INVALID_SOCKET;
	for (;;) {
		if (client->Closed() && flags & flag_closedSignalSent)
		{
			break;
		}

		auto id = client->ReceiveParseTo<SOCKET>();
		auto name = client->ReceiveString();
		auto choose_socket = client->ReceiveParseTo<SOCKET>();
		auto audio_frames = client->ReceiveVec<int16_t>();
		auto pk_size = client->ReceiveParseTo<int32_t>();


		std::vector<std::vector<uint8_t>> packets;
		if (!client->Closed()) {
			if (save_id == INVALID_SOCKET) { // since there always be at least one loop, this "=" will always be done
				save_id = *id;
				println("connected to client socket: " << save_id);
			}

			if (!(flags & flag_clientChoiceRecorded) || (*choose_socket != last_chosen_socket)) {
				{
					std::lock_guard lock(room->m_mutex_choices);
					room->m_choices_of[client->Id()] = *choose_socket;
				}
				last_chosen_socket = *choose_socket;
				flags |= flag_clientChoiceRecorded;
			}

			packets = std::vector<std::vector<uint8_t>>(client->Closed() ? 0 : *pk_size);
			for (auto& pk : packets) {
				auto tmp = client->Receive();
				pk = std::move(*tmp);
			}
		}
		{
			std::lock_guard lock(room->m_mtx_cs);
			std::for_each(room->m_client_sockets.begin(), room->m_client_sockets.end(), [&](std::unique_ptr<Client>& c) {
				if (client->Closed()) {
					c->Send(signal_closed);
					c->Send(save_id);
					flags |= flag_closedSignalSent;
					return;
				}

				bool tmp;
				{
					std::shared_lock lock_choices(room->m_mutex_choices);
					auto choice = room->m_choices_of.find(c->Id());
					tmp = choice == room->m_choices_of.end() || choice->second != *id;
				}
				if (tmp) {
					c->Send(signal_choiceNotMatch);
					c->Send(*id);
					c->Send(*name);
					return;
				}

				c->Send(signal_none);
				c->Send(*id);
				c->Send(*name);
				c->Send(*audio_frames);
				c->Send(*pk_size);
				for (auto& pk : packets) {
					c->Send(pk);
				}
				});
		}
	}

	println("closed client socket: " << save_id);
	{
		std::lock_guard lock(room->m_mtx_need_check);
		room->m_flag |= f_need_check_thread;
	}
	room->m_cv_delete_thread.notify_one();
}

void Room::deleteThread(Room* room) {
	for (;;) {
		{
			std::unique_lock lock(room->m_mtx_need_check);
			room->m_cv_delete_thread.wait(lock, [&room] { 
				return room->m_flag & f_need_check_thread; 
			});
			room->m_flag &= ~f_need_check_thread;
		}
		{
			std::lock_guard lock(room->m_mtx_id);
			if (room->m_id == invalid_id)
				break;
		}
		{
			std::lock_guard lock_cs(room->m_mtx_cs);
			std::lock_guard lock_threads(room->m_mtx_threads);
			for (size_t i = 0; i < room->m_client_sockets.size(); ) {
				if (room->m_client_sockets[i]->Closed()) {
					room->m_clients_threads.erase(room->m_clients_threads.begin() + i);
					room->m_client_sockets.erase(room->m_client_sockets.begin() + i);
				}
				else {
					++i;
				}
			}
		}
	}
}

Room::Room(const std::optional<uint32_t>& passwd) :
	m_passwd(passwd),
	m_delete_thread_thread(deleteThread, this)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFFFF);
	{
		std::lock_guard lock(m_mtx_id);
		while (m_id == invalid_id) {
			m_id = dist(gen);
			std::lock_guard lock(mtx_ids);
			if (Room::ids.end() != std::lower_bound(Room::ids.begin(), Room::ids.end(), m_id)) {
				m_id = invalid_id;
			}
			else {
				ids.push_back(m_id);
			}
		}
	}
	{
		std::lock_guard lock(mtx_ids);
		std::sort(Room::ids.begin(), Room::ids.end());
	}
}

Room::~Room() {
	assert(m_client_sockets.size() == 0);
	//end and join m_delete_thread_thread
	{
		std::lock_guard lock(m_mtx_id);
		m_id = Room::invalid_id;
	}
	{
		std::lock_guard lock(m_mtx_need_check);
		m_flag |= f_need_check_thread;
	}
	m_cv_delete_thread.notify_one();
}

void Room::pushClient(Client c) {
	{
		std::lock_guard lock(m_mtx_used);
		m_flag |= f_used;
	}
	Client* ptr;
	{
		std::lock_guard lock(m_mtx_cs);
		auto tmp = std::make_unique<Client>(std::move(c));
		ptr = tmp.get();
		m_client_sockets.emplace_back(std::move(tmp));
	}
	{
		std::lock_guard lock(m_mtx_threads);
		m_clients_threads.emplace_back(std::jthread(sendMsg, ptr, this));
	}
}