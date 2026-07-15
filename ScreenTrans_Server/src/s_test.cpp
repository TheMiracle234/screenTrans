#define SOCK_DEBUG
#include <Server.h>
#include <Client.h>

#include <thread>
#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <algorithm>
#include <execution>
#include <mutex>
#include <memory>

#define println(x) std::cout<< x << "\n"
#define loop for(;;)

using TM::Server, TM::Client;
std::mutex mtx_cs;

void sendMsg(Client* client, std::vector<std::unique_ptr<Client>>& client_sockets) {
	std::optional<std::string> msg;
	while (msg = client->ReceiveString()) {
		println("recv from " << client->id << ": ");
		println(*msg);
		std::lock_guard lock(mtx_cs);
		std::for_each(client_sockets.begin(), client_sockets.end(), [&](std::unique_ptr<Client>& c) {
				if(c->id != client->id)
					c->Send(*msg);
			}
		);
	}
	std::lock_guard lock(mtx_cs);
	client_sockets.erase(std::remove_if(client_sockets.begin(), client_sockets.end(), [&](std::unique_ptr<Client>& c) {
				return c->id == client->id;
			}
		), client_sockets.end()
	);
}

int main() {
	Server server(Server::TCP, Server::IPV4, 8080);
	server.Listen();
	std::vector<std::jthread> clients_threads;
	std::vector<std::unique_ptr<Client>> client_sockets;
	loop {
		println("waiting for now client...");
		auto c = server.Accept().value();
		{
			std::lock_guard lock(mtx_cs);
			client_sockets.emplace_back(std::make_unique<Client>(std::move(c)));
		}
		clients_threads.emplace_back(
			std::jthread(sendMsg, client_sockets.back().get(), std::ref(client_sockets))
		);
	}
}