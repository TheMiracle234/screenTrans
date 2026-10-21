#pragma once
#include <vector>
#include <cstdint>

namespace ccap{ class Provider; }

class CameraCapture {
public:
	CameraCapture();
	~CameraCapture();

	bool Open(int deviceIndex = 0, int width = 640, int height = 480);
	void Close();
	bool GetFrame(std::vector<uint8_t>& out, int& outW, int& outH);
	bool IsOpened() const;
private:
	ccap::Provider* m_provider=nullptr;
	bool m_opened = false;
};