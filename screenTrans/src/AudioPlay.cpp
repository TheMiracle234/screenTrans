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


#include "AudioPlay.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <algorithm>

#ifdef min
#   undef min
#endif

namespace ST {

    AudioPlay::AudioPlay(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        Init(sampleRate, channels, periodSizeInFrames);
    }

    AudioPlay::~AudioPlay() {
        Stop();
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }
    }

    void AudioPlay::Reset(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        bool wasRunning = m_running;
        Stop();
        Init(sampleRate, channels, periodSizeInFrames);
        if (wasRunning) {
            Start();
        }
    }

    void AudioPlay::Init(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }

		m_sampleRate = sampleRate;
        m_channels = channels;

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16;
        config.playback.channels = channels;

        config.sampleRate = sampleRate;
        config.periodSizeInFrames = periodSizeInFrames;
        config.periods = 2;

        config.dataCallback = DataCallback;
        config.pUserData = this;

        ma_result result = ma_device_init(nullptr, &config, &m_device);
        if (result != MA_SUCCESS) {
            std::cerr << "AudioPlay::Init(...) error" << std::endl;
            system("pause");
            exit(1);
        }

        m_initiated = true;
    }

    bool AudioPlay::Start() {
        auto result = ma_device_start(&m_device);
        if (result != MA_SUCCESS) {
            std::cerr << "AudioPlay::Start() error" << std::endl;
            return false;
        }
        m_running = true;
        return true;
    }

    void AudioPlay::Stop() {
        if (m_running) {
            m_running = false;
            ma_device_stop(&m_device);

            // 等待设备停止
            while (ma_device_is_started(&m_device)) {
                std::this_thread::yield();
            }

            // 等待所有正在执行的回调退出
            while (m_activeCallbacks.load(std::memory_order_acquire) > 0) {
                std::this_thread::yield();
            }
        }
    }

    void AudioPlay::PushFrames(const std::vector<int16_t>& data) {
        if (data.empty())
            return;

        std::lock_guard<std::mutex> lock(m_mtx_buffer);
        // 当前待播放的采样数（未读的）
        size_t pending = m_buffer.size() - m_readPos;
        // 允许最大缓冲 200ms 的数据（交错格式，总采样数 = 采样率 * 通道数 * 0.2 秒）
        const size_t maxPendingSamples = static_cast<size_t>(m_sampleRate * m_channels * DELAY_SEC);

        if (pending + data.size() > maxPendingSamples) {
            // 丢弃整个缓冲区，然后只保留最新的一部分数据（避免全丢造成短暂静音）
            m_buffer.clear();
            m_readPos = 0;
            // 保留最后 maxPendingSamples / 2 个采样（或整个 data，取较小值）
            size_t keep = std::min(data.size(), maxPendingSamples / 2);
            m_buffer.insert(m_buffer.end(), data.end() - keep, data.end());
        }
        else {
            m_buffer.insert(m_buffer.end(), data.begin(), data.end());
        }
    }

    void AudioPlay::PushFrames(std::vector<int16_t>&& data) {
        if (data.empty())
            return;

        std::lock_guard<std::mutex> lock(m_mtx_buffer);
        size_t pending = m_buffer.size() - m_readPos;
        const size_t maxPendingSamples = static_cast<size_t>(m_sampleRate * m_channels * DELAY_SEC);

        if (pending + data.size() > maxPendingSamples) {
            m_buffer.clear();
            m_readPos = 0;
            size_t keep = std::min(data.size(), maxPendingSamples / 2);
            // 移动最后 keep 个元素
            m_buffer.insert(m_buffer.end(),
                std::make_move_iterator(data.end() - keep),
                std::make_move_iterator(data.end()));
        }
        else {
            // 移动全部元素
            m_buffer.insert(m_buffer.end(),
                std::make_move_iterator(data.begin()),
                std::make_move_iterator(data.end()));
        }
    }

    void AudioPlay::Clear() {
        std::lock_guard<std::mutex> lock(m_mtx_buffer);
        m_buffer.clear();
        m_readPos = 0;
    }

    void AudioPlay::DataCallback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frameCount
    ) {
        (void)input;

        auto* self = static_cast<AudioPlay*>(device->pUserData);
        if (!self || !output) return;

        self->m_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);

        // 如果设备未运行，直接输出静音
        if (!self->m_running.load(std::memory_order_relaxed)) {
            size_t samplesToWrite = frameCount * self->m_channels;
            std::memset(output, 0, samplesToWrite * sizeof(int16_t));
            self->m_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }

        int16_t* out = static_cast<int16_t*>(output);
        size_t totalSamplesNeeded = frameCount * self->m_channels;

        {
            std::lock_guard<std::mutex> lock(self->m_mtx_buffer);

            size_t available = self->m_buffer.size() - self->m_readPos;
            if (available >= totalSamplesNeeded) {
                // 有足够数据，直接拷贝
                std::memcpy(out, self->m_buffer.data() + self->m_readPos, totalSamplesNeeded * sizeof(int16_t));
                self->m_readPos += totalSamplesNeeded;
                // 如果缓冲区全部用完，清空并重置 readPos（可选）
                if (self->m_readPos == self->m_buffer.size()) {
                    self->m_buffer.clear();
                    self->m_readPos = 0;
                }
            }
            else {
                // 数据不足：先拷贝已有数据，剩余部分填充静音
                if (available > 0) {
                    std::memcpy(out, self->m_buffer.data() + self->m_readPos, available * sizeof(int16_t));
                    out += available;
                }
                size_t remaining = totalSamplesNeeded - available;
                std::memset(out, 0, remaining * sizeof(int16_t));
                // 清空缓冲区
                self->m_buffer.clear();
                self->m_readPos = 0;
            }
        }

        self->m_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

}