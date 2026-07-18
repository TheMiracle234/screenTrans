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

#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>

#include "ST_API.h"

namespace ST {

    class ST_API AudioPlay
    {
    public:
        static inline float DELAY_SEC = 0.2f;  // 允许最大缓冲 200ms 的数据（交错格式，总采样数 = 采样率 * 通道数 * DELAY_SEC）

        AudioPlay(
            uint32_t sampleRate = 48000,
            uint32_t channels = 1,
            uint32_t periodSizeInFrames = 256
        );

        ~AudioPlay();

        void Reset(
            uint32_t sampleRate = 48000,
            uint32_t channels = 1,
            uint32_t periodSizeInFrames = 256
        );

        bool Start();
        void Stop();
        bool IsRunning() const { return m_running; }

        // 向播放队列添加 PCM 数据（int16_t 交错格式）
        // frameCount: 每个通道的采样数，总数据长度为 frameCount * channels
        void PushFrames(const std::vector<int16_t>& data);
        void PushFrames(std::vector<int16_t>&& data);

        // 清空播放队列中尚未播放的数据
        void Clear();

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
		uint32_t m_sampleRate = 48000;
        std::atomic<bool> m_running = false;
        std::atomic<int> m_activeCallbacks{ 0 };
        uint32_t m_channels = 1;
        ma_device m_device{};

        // 播放缓冲区：使用环形缓冲区或简单 vector + 游标
        std::vector<int16_t> m_buffer;
        size_t m_readPos = 0;          // 下次读取的位置（采样数）
        std::mutex m_mtx_buffer;
    };

}