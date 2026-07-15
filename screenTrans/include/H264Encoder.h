#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "ST_API.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace ST {

    class ST_API H264Encoder {
    public:
        static constexpr int NO_CHANGE = -1;

        H264Encoder(int width, int height, int fps, int bitrate);
        ~H264Encoder() { Destroy(); }

        bool Reset(int width = NO_CHANGE, int height = NO_CHANGE, int fps = NO_CHANGE, int bitrate = NO_CHANGE);
        std::vector<std::vector<uint8_t>> EncodeFrame(const std::vector<uint8_t>& rgba);
        std::vector<std::vector<uint8_t>> Flush();

    private:
        bool Initialize(int width, int height, int fps, int bitrate);
        void Destroy();
        const AVCodec* SelectEncoder(std::string& out_name, bool& out_hw);

    private:
        AVCodecContext* m_ctx = nullptr;
        AVFrame* m_frame = nullptr;
        AVPacket* m_pkt = nullptr;
        SwsContext* m_sws = nullptr;

        int m_width = 0;
        int m_height = 0;
        int m_fps = 0;
        int m_bitrate = 0;
        int64_t m_pts = 0;

        std::string m_encoder_name;
        bool m_is_hw = false;
    };

}