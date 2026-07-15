#include <Server.h>
#include <Client.h>
#include <st_signals.h>

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
#define loop for(;;)

using TM::Server, TM::Client;

std::shared_mutex mtx_cs;
std::mutex mtx_threads;
std::mutex mtx_need_check;
std::shared_mutex mutex_choices;
std::vector<std::unique_ptr<Client>> client_sockets;
std::vector<std::jthread> clients_threads;
std::condition_variable cv_delete_thread;
std::unordered_map<SOCKET, SOCKET> choices_of; // <socket of client(in server), client socket choice>
bool need_check_thread = false;


/*
recv: id, name, choose_socket, audio_frames, pk_size, pk[n]
send: signals, id (, name (, audio_frames, pk_size, pk[n]))

if closed 
	send: signals, id

if choiceNotMatch
	send: signals, id, name

*/
void sendMsg(Client* client) {
	SOCKET save_id = INVALID_SOCKET;

	using Flag = uint8_t;
	enum : Flag{
		flag_closedSignalSent		 = 0x01,
		flag_clientChoiceRecorded	 = 0x02,
	};
	Flag flags = 0;
	SOCKET last_chosen_socket = INVALID_SOCKET;
	loop{
		if (client->Closed() && flags & flag_closedSignalSent)
			{ break; }

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
					std::lock_guard lock(mutex_choices);
					choices_of[client->Id()] = *choose_socket;
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
			std::lock_guard lock(mtx_cs);
			std::for_each(client_sockets.begin(), client_sockets.end(), [&](std::unique_ptr<Client>& c) {
				if (client->Closed()) {
					c->Send(signal_closed);
					c->Send(save_id);
					flags |= flag_closedSignalSent;
					return;
				}

				bool tmp;
				{
					std::shared_lock lock_choices(mutex_choices);
					auto choice = choices_of.find(c->Id());
					tmp = choice == choices_of.end() || choice->second != *id;
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
		std::lock_guard lock(mtx_need_check);
		need_check_thread = true;
	}
	cv_delete_thread.notify_one();
}

void deleteThread() {
	while (true) {
		{
			std::unique_lock lock(mtx_need_check);
			cv_delete_thread.wait(lock, [] { return need_check_thread; });
			need_check_thread = false;
		}

		std::lock_guard lock_cs(mtx_cs);
		std::lock_guard lock_threads(mtx_threads);
		for (size_t i = 0; i < client_sockets.size(); ) {
			if (client_sockets[i]->Closed()) {
				// 先 join 线程，再移除
				if (clients_threads[i].joinable())
					clients_threads[i].join();   // 等待线程自然结束
				client_sockets.erase(client_sockets.begin() + i);
				clients_threads.erase(clients_threads.begin() + i);
			}
			else {
				++i;
			}
		}
	}
}



int main() {
	uint32_t port;
	print("input port to be set: ");
	std::cin >> port;
	Server server(TM::Socket::TCP, TM::Socket::IPV4, port);
	server.Listen();
	println("waiting for the first client...");

	std::jthread delete_thread_thread(deleteThread);

	loop{
		auto c = server.Accept();
		if (!c) {
			println("server accept error");
			continue;
		}
		{
			std::lock_guard lock(mtx_cs);
			client_sockets.emplace_back(std::make_unique<Client>(std::move(*c)));
		}
		{
			std::lock_guard lock(mtx_threads);
			clients_threads.emplace_back(
				std::jthread(sendMsg, client_sockets.back().get())
			);
		}
	}
}