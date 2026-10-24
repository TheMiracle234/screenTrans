#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ST {
    struct DecodedFrame;
}

namespace record {
// RGBA8, tightly packed, row-major; all images in one batch have the same size.
// audio is interleaved float32 PCM (for stereo: L,R,L,R,...).
    
class Mp4Recorder final {
public:
    explicit Mp4Recorder(int outputWidth = 1920, int outputHeight = 1080,
                         int videoFps = 30, int videoBitrate = 6000000);
    ~Mp4Recorder();
    Mp4Recorder(const Mp4Recorder&) = delete;
    Mp4Recorder& operator=(const Mp4Recorder&) = delete;

    // Each nonempty images batch represents exactly ONE output video frame.
    // Empty images: audio-only call; empty audio: video-only call.
    // channels / sampleRate define the recording format on the first call and
    // must remain unchanged. Calls must be serialized by the caller.
    bool addFrame(const std::vector<std::vector<uint8_t>>& images,
                  int imageWidth, int imageHeight,
                  const std::vector<float>& audio,
                  int channels, int sampleRate);
    // Timestamp mode: timestamps in MICROSECONDS relative to the start of recording.
    // Use -1 for an absent stream (empty images or empty audio respectively).
    // Video PTS must move to a strictly later output-frame slot (1/videoFps).
    // Audio PTS marks the FIRST sample of this batch. Audio gaps <= 5s are filled
    // with silence; overlaps are trimmed by the encoder. Large gaps are rejected.
    // Do not mix addFrame() and addFrameTimed() within one recorder instance.
    // Calls must be serialized by the caller; timestamps are not wall-clock times.
    bool addFrameTimed(const std::vector<std::vector<uint8_t>>& images,
                       int imageWidth, int imageHeight, int64_t videoPtsUs,
                       const std::vector<float>& audio,
                       int channels, int sampleRate, int64_t audioPtsUs);


    bool addFrames(const std::vector<std::shared_ptr<ST::DecodedFrame>>& frames,
        const std::vector<float>& audio,
        int channels, int sampleRate);

    bool addFramesTimed(const std::vector<std::shared_ptr<ST::DecodedFrame>>& frames,
        int64_t videoPtsUs,
        const std::vector<float>& audio,
        int channels, int sampleRate, int64_t audioPtsUs);


    // One-shot finalize. Return false if the destination exists or write fails.
    bool save(const std::filesystem::path& outputPath);
    const std::string& lastError() const noexcept;
    int videoFps();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace recording
