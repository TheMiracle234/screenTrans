// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <App.hpp>

void App::register_handle(std::optional<Client> c) {
	if (!c) {
		println("server accept error");
		return;
	}

	enum class Page {
		listen,
		choose,
		make_room,
		enter_room,
		interrupted,
		//end,
	}page{ Page::choose };


	while (1) {
		switch (page) {
		case Page::listen: {
			return;
		}break;
		case Page::choose: {
			auto pre = c->ReceiveParseTo<bool>();
			if (!pre.has_value()) { println(__LINE__); page = Page::interrupted; break; }
			if (*pre) { println(__LINE__); page = Page::listen; break; }
			auto choice = c->ReceiveParseTo<Choice>();
			if (!choice) { page = Page::listen; break; }
			else {
				switch (*choice) {
				case choice_make_room: page = Page::make_room; break;
				case choice_enter_room: page = Page::enter_room; break;
				}
			}
		}break;
		case Page::make_room: {
			auto pre = c->ReceiveParseTo<bool>();
			if (!pre.has_value()) { println(__LINE__); page = Page::interrupted; break; }
			else if (*pre) { println(__LINE__); page = Page::choose; break; }

			auto passwd = c->ReceiveParseTo<uint32_t>();
			if (!passwd.has_value()) { println(__LINE__); page = Page::interrupted; break; }

			auto room = std::make_unique<Room>(*passwd);
			c->Send(room->id());
			println("give id: " << room->id());
			room->pushClient(std::move(*c));
			{
				std::lock_guard lock(mtx_rooms);
				rooms.push_back(std::move(room));
				std::sort(rooms.begin(), rooms.end(), [](std::unique_ptr<Room>& r1, std::unique_ptr<Room>& r2) {
					return r1->id() < r2->id();
					});
			}
			page = Page::listen;
		}break;
		case Page::enter_room: {
			// room id and passwd
			bool enter_ok = true;
			for (;;) {
				auto pre = c->ReceiveParseTo<bool>();
				if (!pre.has_value()) { page = Page::interrupted; break; }
				else if (*pre) { page = Page::choose; break; }
				auto id = c->ReceiveParseTo<uint32_t>();
				auto passwd = c->ReceiveParseTo<uint32_t>();
				if (!id) { page = Page::interrupted; break; }

				// once client gets room, we can't it destruct in the middle
				std::lock_guard lock(mtx_rooms);
				auto room = std::lower_bound(rooms.begin(), rooms.end(), 0, [id](std::unique_ptr<Room>& r, int) { return r->id() < id; });
				if (room == rooms.end() || (*room)->id() != *id) {
					c->Send(false);
					continue;
				}
				else {
					c->Send(true);
				}

				if (!passwd) { page = Page::interrupted; break; }
				if (passwd != (*room)->passwd()) {
					c->Send(false); // failed
					continue;
				}
				else {
					c->Send(true); // success
				}

				(*room)->pushClient(std::move(*c));
				page = Page::listen;
				break;
			}
		}break;
		case Page::interrupted: {
			println("interrupted in the middle");
			page = Page::listen;
		}break;
		}
	}
}

// erase empty and unused rooms every 5 seconds
void App::check_empty_rooms() {
	for (;;) {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		std::vector<uint32_t> record;
		{
			std::lock_guard lock(mtx_rooms);
			std::erase_if(rooms, [&record](std::unique_ptr<Room>& r) {
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

App::App() {

}

void App::run() {
	uint32_t port;
	print("input port to be set: ");
	std::cin >> port;
	Server server(TM::Socket::TCP, TM::Socket::IPV4, port);
	server.Listen();
	println("waiting for the first client...");

	std::jthread t_check_empty_rooms(&App::check_empty_rooms, this);
	for (;;) {
		auto c = server.Accept();
		std::thread register_thread(&App::register_handle, this, std::move(c));
		register_thread.detach();
	}
}

int main() {
	App app{};
	app.run();
}