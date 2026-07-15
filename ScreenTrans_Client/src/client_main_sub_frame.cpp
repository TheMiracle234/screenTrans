#include <Client.h>
#include <iostream>

#include "ScreenCapture.h"
#include "st_util.h"

#define print(x) std::cout<<x
#define println(x) std::cout<<x<<"\n"
#define printv(x) std::cout<<#x<<": "<<x<<"\n"
#define PERROR(x) std::cerr<<__FILE__<<"\nline "<<__LINE__<<": "<<x<<"\n"
#define PAUSE (void)getchar()

using namespace TM;

int main() {
	ScreenCapture capture;
	if (!capture.Initialize()) {
		std::cout << "capture init failed\n";
		return -1;
	}


	Socket::StartUp();

	Client client;
	client.Init(Socket::Protocol::TCP, Socket::Ip::IPV4);
	println("client initialized!");
	print("input server ipv4: ");
	std::string ip;
	std::getline(std::cin, ip);
	client.ConnectTo(ip, 8080);
	println("connected to server!");
	bool first_in = true;
	std::vector<uint8_t> last_data;
	for (;;) {
		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& data = capture.GetBuffer();

		int w = capture.Width();
		int h = capture.Height();

		if (w == 0 || h == 0 || data.empty()) {
			println("captured invalid frame");
			continue;
		}

		if (!client.Send(w)) { println("send w failed"); break; }
		if (!client.Send(h)) { println("send h failed"); break; }
		if (first_in) {
			first_in = false;
			last_data = data;
			if (!client.Send(data)) { println("send msg failed"); break; }
		}
		else {
			auto pd = ST::choose(last_data, data);
			ST::updateLastData(last_data, pd);
			if (pd.idx.size() != 0) {
				if (!client.Send(true)) { println("send has_data failed"); break; }
				if (!client.Send(std::move(pd.idx))) { println("send pd.idx failed"); break; }
				if (!client.Send(pd.data)) { println("send pd.data failed"); break; }
			}
			else {
				if (!client.Send(0)) { println("send has_data failed"); break; }
			}
		}
		client.Receive();
	}


	client.Close();
	Socket::CleanUp();
	
	//PAUSE;
	return 0;
}