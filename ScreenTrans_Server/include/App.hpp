// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
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

class App {
private:
	void register_handle(std::optional<Client> c);
	// erase empty and unused rooms every 5 seconds
	void check_empty_rooms();
public:
	std::mutex mtx_rooms;
	// since in vector there is always copy or move and Room is so big that even move is slow(which we don't allow), so we use unique_ptr as a fast move container
	std::vector<std::unique_ptr<Room>> rooms{};

public:
	App();
	void run();
};