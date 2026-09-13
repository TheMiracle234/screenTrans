#include <audio/User.hpp>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <GLFW/glfw3.h>
#undef max
#undef min
namespace audio {
	User::User(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, int buf_sec) :
		m_channels(channels),
		m_buf(sampleRate * channels * buf_sec)
	{
		assert(sampleRate % periodSizeInFrames == 0);
	}

	void User::pushFrames(const float* frames, size_t samples) {
		m_buf.push(frames, samples);
	}

	void User::callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
		(void)pInput;
		User* user = static_cast<User*>(pDevice->pUserData);
		if (0 == user->m_buf.pop(static_cast<float*>(pOutput), frameCount * user->m_channels)) {
			memset(pOutput, 0x00, sizeof(float) * frameCount * user->m_channels);
		}
	}
}