#define SOCK_DEBUG
#include <Client.h>
#include <Socket.h>

#include <iostream>
#include <thread>

#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
#define loop for(;;)

using TM::Client, TM::Socket;

void recvMsg(Client& client) {
	std::optional<std::string> msg;
	while(!client.Closed() && (msg = client.ReceiveString()) ) {
		println("recv from " << std::to_string(client.id) << ": " << *msg);
		print("> ");
	}
	client.Close();
}

int main() {
	TM::Client client(Socket::TCP, Socket::IPV4);
	client.ConnectTo("127.0.0.1", 8080);
	std::string msg;
	std::jthread recv_thread(recvMsg, std::ref(client));
	while(msg != "exit" && !client.Closed()) {
		print("> ");
		std::getline(std::cin, msg);
		client.Send(msg);
	}
	client.Close();
}