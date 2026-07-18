/*
    screenTrans - online meeting app
    Copyright (C) 2026 Yuan Aowei

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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