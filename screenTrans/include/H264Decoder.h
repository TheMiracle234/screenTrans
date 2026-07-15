#pragma once

#include <vector>
#include <cstdint>

#include "ST_API.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace ST {

    struct ST_API DecodedFrame {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    class ST_API H264Decoder {
    public:
        H264Decoder();
        ~H264Decoder() { Destroy(); }

        std::vector<DecodedFrame> DecodePacket(const std::vector<uint8_t>& packet);
        std::vector<DecodedFrame> Flush();

    private:
        bool Initialize();
        void Destroy();
        AVCodecContext* m_ctx = nullptr;
        AVFrame* m_frame = nullptr;
        AVPacket* m_pkt = nullptr;
        SwsContext* m_sws = nullptr;

        int m_last_width = 0;
        int m_last_height = 0;
        AVPixelFormat m_last_fmt = AV_PIX_FMT_NONE;
    };

}