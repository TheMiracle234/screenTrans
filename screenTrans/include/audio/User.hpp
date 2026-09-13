#include <miniaudio.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/circular_buffer.hpp>
#include <vector>
namespace audio {
	class User {
	private:
		const uint32_t m_channels;
		boost::lockfree::spsc_queue<float> m_buf;
	public:
		User(uint32_t sampleRate, uint32_t channels, uint32_t periodSizeInFrames, int buf_sec);
		void pushFrames(const float* frames, size_t samples);
		static void callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
	};
}