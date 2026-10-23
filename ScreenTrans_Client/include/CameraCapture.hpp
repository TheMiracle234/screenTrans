#pragma once
#include <vector>
#include <cstdint>
#include <ccap_core.h>

class CameraCapture {
public:
	CameraCapture();
	~CameraCapture();

	bool Open(int deviceIndex = 0, int width = 0, int height = 0);
	void Close();
	bool GetFrame(std::vector<uint8_t>& out, int& outW, int& outH);
	bool IsOpened() const;
private:
	ccap::Provider m_provider{};
	bool m_opened = false;
};