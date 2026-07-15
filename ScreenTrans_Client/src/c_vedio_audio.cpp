#include <iostream>
#include <thread>

#include <Client.h>
#include <ScreenCapture.h>
#include <H264Encoder.h>
#include <AudioCapture.h>

#define print(x) std::cout<<x
#define println(x) std::cout<<x<<"\n"
#define printv(x) std::cout<<#x<<": "<<x<<"\n"
#define PERROR(x) std::cerr<<__FILE__<<"\nline "<<__LINE__<<": "<<x<<"\n"
#define PAUSE (void)getchar()

using TM::Socket, TM::Client;
using ST::H264Encoder, ST::ScreenCapture, ST::AudioCapture;

void Send(Client& client, ScreenCapture& capture, AudioCapture& ad_cpt) {
	while (!capture.CaptureFrame());
	int w = capture.Width();
	int h = capture.Height();
	int fps = 30;
	int bitrate = 4000000;
	ST::H264Encoder encoder(w, h, fps, bitrate);

	//bool ok = true;
	while (!client.Closed()) {
		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& rgba = capture.GetBuffer();

		w = capture.Width();
		h = capture.Height();

		if (rgba.empty()) {
			println("captured invalid frame");
			continue;
		}

		auto packets = encoder.EncodeFrame(rgba);

		int pk_size = (int)packets.size();

		client.Send(ad_cpt.Frames());
		client.Send(pk_size);

		for (auto& pk : packets) {
			client.Send(pk);
		}

	}
}

int main() {
	ScreenCapture capture;
	if (!capture.Initialize()) {
		std::cout << "capture init failed\n";
		return -1;
	}

	AudioCapture ad_cpt(48000, 1, 256);
	ad_cpt.Start();

	// use this block to destory client autoly
	Client client(Socket::TCP, Socket::IPV4);
	println("client initialized!");
	print("input server ipv4: ");
	std::string ip;
	std::getline(std::cin, ip);
	client.ConnectTo(ip, 8080);
	println("connected to server!");

	std::jthread send_t(Send, std::ref(client), std::ref(capture), std::ref(ad_cpt));

	//system("pause");
	return 0;
}