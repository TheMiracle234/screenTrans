#pragma once

#include <vector>
#include <thread>

namespace ST {

	inline int CORES = std::thread::hardware_concurrency();

	struct pos_change {
		std::vector<int> idx;
		std::vector<uint8_t> data;
	};

	// minu frame
	pos_change choose(const std::vector<uint8_t>& last, const std::vector<uint8_t>& now, uint8_t err = 5);
	void updateLastData(std::vector<uint8_t>& last, const pos_change& pd);

	// H264
	std::vector<uint8_t> compress_bytes_to_h264_auto(
		const std::vector<uint8_t>& bytes_data,
		int width, int height,
		int fps = 60, int bitrate = 4000000);

	std::vector<uint8_t> decompress_h264_to_bytes(
		const std::vector<uint8_t>& h264,
		int& out_width,
		int& out_height);
}