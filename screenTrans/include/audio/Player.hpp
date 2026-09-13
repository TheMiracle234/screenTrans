#pragma once

#include <miniaudio.h>
#include <cstdint>
#include <mutex>

namespace audio {

    class Player {
    private:
        using Flag = uint8_t;
        enum : Flag {
            flag_initiated = (Flag)1 << 0,
            flag_isRunning = (Flag)1 << 1,
        };
        Flag m_flag{ 0 };
        uint32_t m_channels;
        uint32_t m_periodSizeInFrames;
        ma_device m_device;

    public:
        Player(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack);
        ~Player();
        void reset(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack);
        bool start();
        void stop();
        const ma_device& device() const { m_device; }
        uint32_t periodSizeInFrames() { return m_periodSizeInFrames; }
    private:
        void init(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, void* user, ma_device_data_proc dataCallBack);
    };
}