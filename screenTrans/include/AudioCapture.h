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

#include <miniaudio.h>

#include <functional>
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>

#include "ST_API.h"

namespace ST {

    class ST_API AudioCapture
    {
    public:
        AudioCapture(
            uint32_t sampleRate = 48000,
            uint32_t channels = 1,
            uint32_t periodSizeInFrames = 256
        );

        ~AudioCapture();

        void Reset(
            uint32_t sampleRate = 48000,
            uint32_t channels = 1,
            uint32_t periodSizeInFrames = 256
        );

        bool Start();
        void Stop();
        bool IsRunning() const { return m_running; }

        // std::move
        std::vector<int16_t> Frames();

    private:
        static void DataCallback(
            ma_device* device,
            void* output,
            const void* input,
            ma_uint32 frameCount
        );

        void Init(
            uint32_t sampleRate = 48000,
            uint32_t channels = 1,
            uint32_t periodSizeInFrames = 256
        );

    private:
        bool m_initiated = false;
        std::atomic<bool> m_running = false;
        std::atomic<int> m_activeCallbacks{ 0 };
        // uint32_t m_sampleRate = 48000;
        uint32_t m_channels = 1;
        // uint32_t m_periodSizeInFrames = 256;
        ma_device m_device{};
        std::vector<int16_t> m_frames;
        std::mutex m_mtx_fs{};
    };

}