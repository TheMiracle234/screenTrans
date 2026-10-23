#include <audio/User.hpp>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <GLFW/glfw3.h>
#undef max
#undef min

namespace audio {

    User::User(uint32_t sampleRate, uint32_t channels,
        uint32_t periodSizeInFrames, double buf_sec)
        : m_channels(channels)
        , m_buf(static_cast<size_t>(sampleRate* channels* buf_sec))
    {
        assert(sampleRate % periodSizeInFrames == 0);
    }

    void User::pushFrames(const float* frames, size_t samples) {
#ifdef USE_MUTEX
        std::lock_guard lk(m_buf_mtx);
        // circular_buffer 满了自动覆盖最旧元素 = 丢旧保新
        m_buf.insert(m_buf.end(), frames, frames + samples);
#else
        // spsc_queue 满了丢弃新数据 = 丢新
        m_buf.push(frames, samples);
#endif
    }

    void User::callback(ma_device* pDevice, void* pOutput,
        const void* pInput, ma_uint32 frameCount) {
        (void)pInput;
        User* user = static_cast<User*>(pDevice->pUserData);
        const size_t total = static_cast<size_t>(frameCount) * user->m_channels;

#ifdef USE_MUTEX
        size_t got;
        {
            std::lock_guard lk(user->m_buf_mtx);
            got = std::min(total, user->m_buf.size());
            std::copy_n(user->m_buf.begin(), got, static_cast<float*>(pOutput));
            user->m_buf.erase_begin(got);   // O(1) 丢弃头部
        }
#else
        size_t got = user->m_buf.pop(static_cast<float*>(pOutput), total);
#endif

        if (got < total) {
            std::memset(static_cast<float*>(pOutput) + got, 0,
                sizeof(float) * (total - got));
        }
    }

    void User::clearLeft() {
#ifdef USE_MUTEX
        std::lock_guard lk(m_buf_mtx);
        m_buf.clear();
#else
        m_buf.reset();
#endif
    }

} // namespace audio