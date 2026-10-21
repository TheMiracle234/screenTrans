#include "CameraCapture.hpp"
#include "ccap_convert.h"
#include "ccap.h"
#include <cstring>
#include <iostream>

CameraCapture::CameraCapture() {
	m_provider = new ccap::Provider();
    //设置错误回调、日志等级
    //ccap::setErrorCallback([](ccap::ErrorCode code, std::string_view desc) {
    //    std::cerr << "[ccap摄像头错误] code=" << (int)code << " " << desc << std::endl;
    //    });
    //ccap::setLogLevel(ccap::LogLevel::Verbose);
}

CameraCapture::~CameraCapture() {
	Close();
	delete m_provider;
}

bool CameraCapture::Open(int deviceId, int w, int h) {
	if(!m_provider) return false;
	if (m_opened) Close();

	m_provider->set(ccap::PropertyName::PixelFormatInternal, ccap::PixelFormat::NV12);
	m_provider->set(ccap::PropertyName::PixelFormatOutput, ccap::PixelFormat::BGRA32);
	m_provider->set(ccap::PropertyName::Width, w);
	m_provider->set(ccap::PropertyName::Height, h);

	bool opened = m_provider->open(deviceId, true);
	if (!opened) {
		std::cerr << "[摄像头] open失败，设备id:" << deviceId << std::endl;
		return false;
	}
	if (!m_provider->isStarted()){
		std::cerr << "[摄像头] 设备打开但是没有启动" << std::endl;
		return false;
	}

	m_opened = true;
	return true;
}

bool CameraCapture::GetFrame(std::vector<uint8_t>& out, int& outW, int& outH){
	if (!m_opened || !m_provider) return false;

	auto frame = m_provider->grab(3000);
	if (!frame) return false;

	outW = static_cast<int>(frame->width);
	outH = static_cast<int>(frame->height);
	size_t targetBytes = (size_t)outW * outH * 4;
	out.resize(targetBytes);

	if (frame->sizeInBytes >= targetBytes) {
		memcpy(out.data(), frame->data[0], targetBytes);
	}
	else if (frame->sizeInBytes == (size_t)outW * outH * 3) {
		const uint8_t* src = frame->data[0];
		uint8_t* dst = out.data();
		for (size_t i = 0; i < (size_t)outW * outH; ++i) {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = 255;
			src += 3;
			dst += 4;
		}
	}
	else {
		std::cerr << "Unknown frame size: " << frame->sizeInBytes << std::endl;
		return false;
	}
	return true;
}

bool CameraCapture::IsOpened() const{
	return m_opened;
}

void CameraCapture::Close() {
	if (m_opened && m_provider) {
		m_provider->close();
	}
	m_opened = false;
}