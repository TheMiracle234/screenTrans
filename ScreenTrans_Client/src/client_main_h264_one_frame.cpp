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

		int fps = client.ReceiveParseTo<int>();
		int bitrate = client.ReceiveParseTo<int>();
		auto h264 = ST::compress_bytes_to_h264_auto(data, w, h, fps, bitrate);
		if (!client.Send(h264)) { println("send h264 failed"); break; }
	}


	client.Close();
	Socket::CleanUp();

	//PAUSE;
	return 0;
}