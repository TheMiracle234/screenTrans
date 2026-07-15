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