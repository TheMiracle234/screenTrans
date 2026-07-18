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