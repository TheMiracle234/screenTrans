#include <Server.h>
#include <Client.h>
#include <st_signals.h>
#include <Room.h>

#include <thread>
#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <algorithm>
#include <execution>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include <unordered_map>

#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
//#define pv(x) std::cout<< #x << ": " << x << "\n"

using TM::Server, TM::Client;

std::mutex mtx_rooms;
// since in vector there is always copy or move and Room is so big that even move is slow(which we don't allow), so we use unique_ptr as a fast move container
std::vector<std::unique_ptr<Room>> rooms;

static void register_handle(std::optional<Client> c) {
	if (!c) {
		println("server accept error");
		return;
	}

	// choice
	auto choice = c->ReceiveParseTo<Choice>();
	if (!choice) {
		goto err_interrupted;
	}

	switch (*choice) {
	case choice_make_room: {
		auto passwd = c->ReceiveParseTo<uint32_t>();
		if (!passwd.has_value()) {
			goto err_interrupted;
		}

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
	} break;
	case choice_enter_room: {
		// room id and passwd
		bool enter_ok = true;
		for (;;) {
			auto id = c->ReceiveParseTo<uint32_t>();
			auto passwd = c->ReceiveParseTo<uint32_t>();
			if (!id) {
				goto err_interrupted;
			}

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

			if (!passwd) {
				goto err_interrupted;
			}
			if (passwd != (*room)->passwd()) {
				c->Send(false); // failed
				continue;
			}
			else {
				c->Send(true); // success
			}

			(*room)->pushClient(std::move(*c));
			break;
		}
	} break;
	}

	println("connected successfully");
	return;

err_interrupted:
	println("interrupted in the middle");
}

// erase empty and unused rooms every 5 seconds
void check_empty_rooms() {
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

int main() {
	uint32_t port;
	print("input port to be set: ");
	std::cin >> port;
	Server server(TM::Socket::TCP, TM::Socket::IPV4, port);
	server.Listen();
	println("waiting for the first client...");

	std::jthread t_check_empty_rooms(check_empty_rooms);
	for (;;) {
		auto c = server.Accept();
		std::thread register_thread(register_handle, std::move(c));
		register_thread.detach();
	}
}