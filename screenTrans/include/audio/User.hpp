#pragma once
#include <miniaudio.h>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>

#define USE_MUTEX

#ifdef USE_MUTEX
#   include <mutex>
#   include <boost/circular_buffer.hpp>
#else
#   include <boost/lockfree/spsc_queue.hpp>
#endif

namespace audio {
    static_assert(sizeof(float) == 4);
    class User {
    private:
        const uint32_t m_channels;

#ifdef USE_MUTEX
        mutable std::mutex m_buf_mtx;
        boost::circular_buffer<float> m_buf;
#else
        boost::lockfree::spsc_queue<float> m_buf;
#endif

    public:
        User(uint32_t sampleRate, uint32_t channels,
            uint32_t periodSizeInFrames, double buf_sec);
        void pushFrames(const float* frames, size_t samples);
        void clearLeft();
        static void callback(ma_device* pDevice, void* pOutput,
            const void* pInput, ma_uint32 frameCount);
    };
}