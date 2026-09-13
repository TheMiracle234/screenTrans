// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "audio/Capture.hpp"
#include <iostream>
#include <thread>

namespace audio {

    Capture::Capture(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames,
        int bufSec
    ):
        m_buf(sampleRate* channels* bufSec)
    {
        Init(sampleRate, channels, periodSizeInFrames);
    }

    void Capture::Reset(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        bool is_running = m_running;
        Stop();
        Init(sampleRate, channels, periodSizeInFrames);
        if (is_running) {
            Start();
        }
    }

    Capture::~Capture()
    {
        Stop();
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }
    }

    void Capture::Init(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }

        m_channels = channels;

        ma_device_config config =
            ma_device_config_init(ma_device_type_capture);

        config.capture.format = ma_format_f32;
        config.capture.channels = channels;

        config.sampleRate = sampleRate;

        config.periodSizeInFrames = periodSizeInFrames;
        config.periods = 2;

        config.dataCallback = DataCallback;
        config.pUserData = this;

        ma_result result =
            ma_device_init(nullptr, &config, &m_device);

        if (result != MA_SUCCESS) {
            std::cerr << "Capture::init(...) error" << std::endl;
            system("pause");
            exit(1);
        }

        m_initiated = true;
    }

    bool Capture::Start() {
        auto result = ma_device_start(&m_device);
        if (result != MA_SUCCESS)
        {
            std::cerr << "Capture::Start() error" << std::endl;
            return false;
        }
        m_running = true;
        return true;
    }

    void Capture::Stop()
    {
        if (m_running)
        {
            m_running = false;
            ma_device_stop(&m_device);
            // 等待设备状态变为已停止
            while (ma_device_is_started(&m_device)) {
                std::this_thread::yield();  // 出让时间片，避免忙等占用CPU
            }
            while (m_activeCallbacks.load(std::memory_order_acquire) > 0) {
                std::this_thread::yield();
            }
        }
    }

    std::vector<float> Capture::Frames() {
        std::vector<float> res(m_buf.read_available());
        m_buf.pop(res.data());
        return res;
    }

    void Capture::DataCallback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frameCount)
    {
        (void)output;
        auto* self = static_cast<Capture*>(device->pUserData);
        if (!self || !input || !self->m_running.load(std::memory_order_relaxed)) { return; }
        self->m_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const float* pcm = static_cast<const float*>(input);
        self->m_buf.push(pcm, frameCount * self->m_channels);
        self->m_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

}