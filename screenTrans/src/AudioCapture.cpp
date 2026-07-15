#include "AudioCapture.h"
#include <iostream>
#include <thread>

namespace ST {

    AudioCapture::AudioCapture(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        Init(sampleRate, channels, periodSizeInFrames);
    }

    void AudioCapture::Reset(
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

    AudioCapture::~AudioCapture()
    {
        Stop();
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }
    }

    void AudioCapture::Init(
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

        config.capture.format = ma_format_s16;
        config.capture.channels = channels;

        config.sampleRate = sampleRate;

        config.periodSizeInFrames = periodSizeInFrames;
        config.periods = 2;

        config.dataCallback = DataCallback;
        config.pUserData = this;

        ma_result result =
            ma_device_init(nullptr, &config, &m_device);

        if (result != MA_SUCCESS) {
            std::cerr << "AudioCapture::init(...) error" << std::endl;
            system("pause");
            exit(1);
        }

        m_initiated = true;
    }

    bool AudioCapture::Start() {
        auto result = ma_device_start(&m_device);
        if (result != MA_SUCCESS)
        {
            std::cerr << "AudioCapture::Start() error" << std::endl;
            return false;
        }
        m_running = true;
        return true;
    }

    void AudioCapture::Stop()
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

    std::vector<int16_t> AudioCapture::Frames() {
        std::lock_guard<std::mutex> lock(m_mtx_fs);
        return std::move(m_frames);
    }

    void AudioCapture::DataCallback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frameCount)
    {
        (void)output;

        auto* self = static_cast<AudioCapture*>(device->pUserData);
        if (!self || !input || !self->m_running.load(std::memory_order_relaxed))
            return;

        self->m_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);

        const int16_t* pcm = static_cast<const int16_t*>(input);
        std::lock_guard<std::mutex> lock(self->m_mtx_fs);
        self->m_frames.insert(self->m_frames.end(), pcm, pcm + frameCount * self->m_channels);

        self->m_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

}