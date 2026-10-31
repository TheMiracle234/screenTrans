// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <ServerApp.hpp>
#include <thread>
#include <chrono>
using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""s;

void ServerApp::register_handle(std::optional<net::tcp::Client> c) {
	if (!c) {
		println("server accept error");
		return;
	}


	Connect_t connect_type;
	if (!c->ReceiveBy(connect_type)) { return; }

	if (connect_type == connect_audio) {
		pushAudioConnection(std::move(*c));
		return;
	}

	enum class Page {
		listen,
		choose,
		make_room,
		enter_room,
		interrupted, // client closed
		//end,
	} page{ Page::choose };

	const user::id_t user_id = Room::makeId(c->Id());
	c->Send(user_id);
	bool audio_connection_consumed{ false };
	bool sync_connection;
	if(!c->ReceiveBy(sync_connection)){ // not sure whether audio connection is built
		(void)waitAudioConnection(user_id); // things dealed inside
		return;
	}

	auto audio_client = waitAudioConnection(user_id);
	assert(audio_client);
	audio_connection_consumed = true;


	while (1) {
		switch (page) {
		case Page::listen: {
			return;
		}break;
		case Page::choose: {
			bool pre{};
			if (!c->ReceiveBy(pre))		{ page = Page::interrupted; break; }
			if (pre)					{ page = Page::interrupted; break; }
			Choice choice{};
			if (!c->ReceiveBy(choice))	{ page = Page::interrupted; break; }
			else {
				switch (choice) {
				case choice_make_room: page = Page::make_room; break;
				case choice_enter_room: page = Page::enter_room; break;
				}
			}
		}break;
		case Page::make_room: {
			bool pre{};
			if (!c->ReceiveBy(pre))		{ page = Page::interrupted; break; }
			else if (pre)				{ page = Page::choose; break; }
			uint32_t passwd{};
			if (!c->ReceiveBy(passwd))	{ page = Page::interrupted; break; }

			std::shared_ptr<Room> room;

			room = std::make_shared<Room>(passwd);
			c->Send(room->id());
			println("give id: " << room->id());

			room->pushClient(std::move(*c), std::move(*audio_client));
			{
				std::lock_guard lock(mtx_rooms);
				rooms.push_back(std::move(room));
				std::sort(rooms.begin(), rooms.end(), [](std::shared_ptr<Room>& r1, std::shared_ptr<Room>& r2) {
					return r1->id() < r2->id();
					});
			}
			page = Page::listen;
		}break;
		case Page::enter_room: {
			// room id and passwd
			for (;;) {
				bool pre{};
				if (!c->ReceiveBy(pre)) { page = Page::interrupted; break; }
				else if (pre) { page = Page::choose; break; }
				uint32_t id{}, passwd{};
				if (!c->ReceiveBy(id)) { page = Page::interrupted; break; }
				if (!c->ReceiveBy(passwd)) { page = Page::interrupted; break; }

				std::shared_ptr<Room> room;
				// once client gets room, we can't let it destruct in the middle
				{
					std::lock_guard lock(mtx_rooms);
					auto room0 = std::lower_bound(rooms.begin(), rooms.end(), 0, [id](std::shared_ptr<Room>& r, int) { return r->id() < id; });
					if (room0 == rooms.end() || (*room0)->id() != id) {
						room = nullptr;
					}
					else {
						room = *room0;
					}
				}
				if (!room) {
					c->Send(false);
					continue;
				}
				else {
					c->Send(true);
				}

				if (!passwd) { 
					page = Page::interrupted; 
					break; 
				}
				if (passwd != room->passwd()) {
					c->Send(false); // failed
					continue;
				}
				else {
					c->Send(true); // success
				}

				//println("sleeping");
				//std::this_thread::sleep_for(5s);
				//println("sleep over");

				{
					std::lock_guard lock(mtx_rooms);
					if (room->empty()) [[unlikely]] {
						c->Send(false); // others left while entering
						continue;
					}
					else {
						c->Send(true);
					}
					room->pushClient(std::move(*c), std::move(*audio_client));
				}
				page = Page::listen;
				break;
			}
		}break;
		case Page::interrupted: {
			println("interrupted in the middle");
			std::lock_guard lock(mtx_audio_connections);
			auto itr0 = audio_connections.find(user_id);
			if (itr0 != audio_connections.end()) { // have pushed audio connection
				audio_connections.erase(itr0);
				return;
			}
			// now audio_client has always pushed connection
			return;
		}break;
		}
	}
}

// erase empty and unused rooms every 5 seconds
void ServerApp::check_empty_rooms() {
	for (;;) {
		std::this_thread::sleep_for(5s);
		std::vector<uint32_t> record;
		{
			std::lock_guard lock(mtx_rooms);
			std::erase_if(rooms, [&record](std::shared_ptr<Room>& r) {
				if (r->empty() && r->used()) {
					record.push_back(r->id());
					return true;
				}
				else {
					return false;
				}
				});
		}
		if (record.empty()) {
			continue;
		}
		println("erased rooms in this loop:");
		for (auto i : record) {
			print(i << " ");
		}
		println("");
	}
}

void ServerApp::pushAudioConnection(net::tcp::Client audioClient) {
	struct {
		user::id_t user_id;
	} rv;
	if (!audioClient.ReceiveBy(rv.user_id)) { return; }
	{
		std::lock_guard lock(mtx_audio_connections);
		auto itr0 = audio_connections.find(rv.user_id);
		if (itr0 != audio_connections.end()) {
			if (itr0->second == std::nullopt) { // signal from user client: audio connection should close
				audio_connections.erase(itr0);
				return;
			}
			else { // other user's audio connection, meaning that this user client has closed
				return;
			}
		}
		auto [itr, ok] = audio_connections.try_emplace(rv.user_id, std::move(audioClient));
		if (!itr->second->Send(true)) { // sync_connection
			itr->second->Close();
		}
	}
}

std::optional<net::tcp::Client> ServerApp::waitAudioConnection(user::id_t id) {
	decltype(audio_connections)::iterator a_it;
	bool sync_error{ false };
	util::Timer timer;
	timer.restart();
	for (;;) {
		{
			std::lock_guard lock(mtx_audio_connections);
			a_it = audio_connections.find(id);
			if (a_it != audio_connections.end()) { 
				if (!sync_error) [[likely]] { // normal
					break; 
				}
				else { // audio connection has been built but user client interrupted
					break;
				}
			}
		}
		// meaning that client.ReceiveBy(sync_connection) == false, and we need to find out whther audio_connection has been pushed
		sync_error = true;
		std::this_thread::sleep_for(500ms);
		if (timer.seconds() > 5.0) { // assume audio connection not built(since audio connection built time should be << 5s)
			return {};
		}
	}
	auto res = std::move(a_it->second);
	audio_connections.erase(a_it);
	return res;
}

ServerApp::ServerApp() {

}

void ServerApp::run() {
	uint16_t port;
	print("input port to be set: ");
	std::cin >> port;
	net::tcp::Server server( net::tcp::Ip::Auto, port);
	server.Listen();
	println("waiting for the first client...");

	std::jthread t_check_empty_rooms(&ServerApp::check_empty_rooms, this);
	for (;;) {
		auto c = server.Accept();
		std::thread register_thread(&ServerApp::register_handle, this, std::move(c));
		register_thread.detach();
	}
}

int main() {
	ServerApp app{};
	app.run();
}