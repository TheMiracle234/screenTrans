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

#include <Room.h>
#include <iostream>
#include <st_signals.h>
#include <cassert>
#include <random>
#include <execution>

#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
//#define pv(x) std::cout<< #x << ": " << x << "\n"

/*
recv: id, name, audio_frames, choose_socket, pk_size, pk[n]
send: signals, id (, name, audio_frames (, pk_size, pk[n]))

if closed
	send: signals, id

if choiceNotMatch
	send: signals, id, name, audio_frames

*/
void Room::sendMsg(Client* client, Room* room) {
	SOCKET save_id = INVALID_SOCKET;

	using Flag0 = uint8_t;
	enum : Flag0 {
		flag_closedSignalSent = (Flag0)1 << 0,
		flag_clientChoiceRecorded = (Flag0)1 << 1,
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
		auto audio_frames = client->ReceiveVec<int16_t>();
		auto choose_socket = client->ReceiveParseTo<SOCKET>();
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
			std::shared_lock lock(room->m_mtx_cs);
			std::for_each(std::execution::par, room->m_client_sockets.begin(), room->m_client_sockets.end(), [&](std::unique_ptr<Client>& c) {
				std::lock_guard lock(c->mutex());

				if (client->Closed()) {
					c->Send(signal_closed);
					c->Send(save_id);
					flags |= flag_closedSignalSent;
					return;
				}

				bool choice_isnt_me;
				{
					std::shared_lock lock_choices(room->m_mutex_choices);
					auto choice = room->m_choices_of.find(c->Id());
					choice_isnt_me = choice == room->m_choices_of.end() || choice->second != *id;
				}
				if (choice_isnt_me) {
					c->Send(signal_choiceNotMatch);
					c->Send(*id);
					c->Send(*name);
					c->Send(*audio_frames);
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
	room->m_cs_dead_threads.release();
}

void Room::deleteThread(Room* room) {
	for (;;) {
		room->m_cs_dead_threads.acquire();
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
	m_cs_dead_threads.release();
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