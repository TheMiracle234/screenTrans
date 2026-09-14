// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <miniaudio.h>

#include <functional>
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>

#include <boost/lockfree/spsc_queue.hpp>

#include "ST_API.h"

namespace audio {

    class ST_API Capture
    {
    public:
        Capture(
            uint32_t sampleRate,
            uint32_t channels,
            uint32_t periodSizeInFrames,
            int bufSec
        );

        ~Capture();

        void Reset(
            uint32_t sampleRate = 44100,
            uint32_t channels = 2,
            uint32_t periodSizeInFrames = 1024
        );

        bool Start();
        void Stop();
        bool IsRunning() const { return m_running; }

        // std::move
        std::vector<float> Frames();

    private:
        static void DataCallback(
            ma_device* device,
            void* output,
            const void* input,
            ma_uint32 frameCount
        );

        void Init(
            uint32_t sampleRate = 44100,
            uint32_t channels = 2,
            uint32_t periodSizeInFrames = 1024
        );

    private:
        bool m_initiated = false;
        std::atomic<bool> m_running = false;
        std::atomic<int> m_activeCallbacks{ 0 };
        uint32_t m_channels = 2;
        ma_device m_device{};
        //std::vector<float> m_frames;
        boost::lockfree::spsc_queue<float> m_buf;
        //std::mutex m_mtx_fs{};
    };

}