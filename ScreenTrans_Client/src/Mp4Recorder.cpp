#include "Mp4Recorder.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cassert>

#include <H264Decoder.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
namespace fs = std::filesystem;

namespace record {
    namespace {
        struct RecorderConfig {
            int outputWidth = 1920, outputHeight = 1080, fps = 30, videoBitrate = 6000000;
            int audioSampleRate = 16000, audioChannels = 1, audioBitrate = 128000;
            uint64_t maxQueueBytes = 128ull*1024*1024;
        };
        namespace Result {
            constexpr int Ok = 0, InvalidArgument = -1, InvalidState = -2, QueueFull = -3, CodecError = -4,
                IoError = -5;
        }
        enum class FrameKind {
            Video = 1,
            Audio = 2
        };
        struct RawFrame {
            FrameKind type = FrameKind::Video;
            int64_t pts_us = 0;
            const void* data = nullptr;
            int width = 0, height = 0, stride_bytes = 0, sample_rate = 0, channels = 0, sample_frames = 0;
        };
        std::string utf8(const fs::path& path) {
            auto s = path.u8string();
            return std::string(s.begin(), s.end());
        }
        struct FrameJob {
            int type = 0;
            int64_t pts = 0;
            int w = 0, h = 0, stride = 0, sr = 0, ch = 0, n = 0;
            std::vector<uint8_t> bytes;
        };
        class Core {
            public:
            explicit Core(RecorderConfig cfg):c(cfg) {
                if (c.outputWidth <= 0 || c.outputHeight <= 0 || (c.outputWidth&1) || (c.outputHeight&1)
                    || c.fps <= 0 || c.audioSampleRate <= 0 || c.audioChannels <= 0
                        || c.audioChannels>2) throw std::runtime_error("bad config");
                if (!c.maxQueueBytes)c.maxQueueBytes = 24ull*1024*1024;
                auto tmp = fs::temp_directory_path();
                auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
                temp = tmp/(L"standalone_mp4_"+std::to_wstring(static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())))+L"_"+std::to_wstring(tick)+L".mp4");
                init();
                worker = std::thread([this]{run();});
            }
            ~Core() {
                stop();
                if (worker.joinable())worker.join();
                release();
                std::error_code ec;
                fs::remove(temp, ec);
            }
            int add(const RawFrame& f) {
                if (!f.data || f.pts_us<0) return Result::InvalidArgument;
                FrameJob j;
                j.type = static_cast<int>(f.type);
                j.pts = f.pts_us;
                if (f.type == FrameKind::Video) {
                    if (f.width != c.outputWidth || f.height != c.outputHeight || f.stride_bytes<f.width*4
                        || f.stride_bytes>f.width*32) return Result::InvalidArgument;
                    j.w = f.width;
                    j.h = f.height;
                    j.stride = f.width*4;
                    j.bytes.resize(size_t(j.stride)*j.h);
                    auto* p = static_cast<const uint8_t*>(f.data);
                    for (int y = 0;y<j.h;y++)std::memcpy(j.bytes.data()+size_t(y)*j.stride,
                        p+size_t(y)*f.stride_bytes, j.stride);
                }
                else if (f.type == FrameKind::Audio) {
                    if (f.sample_rate != c.audioSampleRate || f.channels != c.audioChannels
                        || f.sample_frames <= 0 || f.sample_frames>c.audioSampleRate) return Result::InvalidArgument;
                    j.sr = f.sample_rate;
                    j.ch = f.channels;
                    j.n = f.sample_frames;
                    j.bytes.resize(size_t(j.n)*j.ch*sizeof(float));
                    std::memcpy(j.bytes.data(), f.data, j.bytes.size());
                }
                else return Result::InvalidArgument;
                std::lock_guard lk(mu);
                if (stopping || failed) return Result::InvalidState;
                if (j.bytes.size()>c.maxQueueBytes || bytes>c.maxQueueBytes-j.bytes.size())
                    return Result::QueueFull;
                bytes+=j.bytes.size();
                q.push_back(std::move(j));
                cv.notify_one();
                return Result::Ok;
            }
            int save(const fs::path& dst) {
                if (dst.empty()) return Result::InvalidArgument;
                {
                    std::lock_guard lk(mu);
                    if (stopping) return Result::InvalidState;
                    stopping = true;
                    cv.notify_all();
                }
                if (worker.joinable())worker.join();
                int status = failed?Result::CodecError:Result::Ok;
                if (status == Result::Ok) {
                    if (flush(vcodec, vstream)<0 || flush(acodec, astream)<0
                        || av_write_trailer(fmt)<0) status = Result::IoError;
                }
                release();
                if (status != Result::Ok) return status;
                std::error_code ec;
                if ((!dst.parent_path().empty() && !fs::exists(dst.parent_path(), ec)) || fs::exists(dst,
                    ec)) return Result::IoError;
                fs::rename(temp, dst, ec);
                if (ec) {
                    ec.clear();
                    fs::copy_file(temp, dst, fs::copy_options::none, ec);
                    if (ec) return Result::IoError;
                    std::error_code ec2;
                    fs::remove(temp, ec2);
                }
                saved = true;
                return Result::Ok;
            }
            private:
            RecorderConfig c{};
            fs::path temp;
            AVFormatContext* fmt = nullptr;
            AVCodecContext *vcodec = nullptr, *acodec = nullptr;
            AVStream *vstream = nullptr, *astream = nullptr;
            SwsContext* sws = nullptr;
            SwrContext* swr = nullptr;
            AVFrame* vframe = nullptr;
            AVPacket* pkt = nullptr;
            std::mutex mu;
            std::condition_variable cv;
            std::deque<FrameJob> q;
            uint64_t bytes = 0;
            bool stopping = false, saved = false;
            std::atomic<bool> failed{false};
            std::thread worker;
            int64_t lastVideo = -1, audioSamples = 0;
            std::vector<float> pendingAudio;
            size_t pendingPos = 0;
            void init() {
                try {
                    const auto filename = utf8(temp);
                    if (avformat_alloc_output_context2(&fmt, nullptr, "mp4", filename.c_str())<0
                        || !fmt) throw std::runtime_error("mux init");
                    const AVCodec* vc = avcodec_find_encoder_by_name("libx264");
                    if (!vc)vc = avcodec_find_encoder(AV_CODEC_ID_H264);
                    if (!vc) throw std::runtime_error("H264 unavailable");
                    vstream = avformat_new_stream(fmt, nullptr);
                    if (!vstream) throw std::runtime_error("video stream");
                    vcodec = avcodec_alloc_context3(vc);
                    if (!vcodec) throw std::runtime_error("video ctx");
                    vcodec->codec_id = AV_CODEC_ID_H264;
                    vcodec->width = c.outputWidth;
                    vcodec->height = c.outputHeight;
                    vcodec->pix_fmt = AV_PIX_FMT_YUV420P;
                    vcodec->time_base = {1, c.fps};
                    vcodec->framerate = {c.fps, 1};
                    vcodec->bit_rate = c.videoBitrate>0?c.videoBitrate:3000000;
                    vcodec->gop_size = c.fps*2;
                    vcodec->max_b_frames = 0;
                    if (fmt->oformat->flags&AVFMT_GLOBALHEADER)vcodec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
                    AVDictionary* opts = nullptr;
                    av_dict_set(&opts, "preset", "veryfast", 0);
                    int r = avcodec_open2(vcodec, vc, &opts);
                    av_dict_free(&opts);
                    if (r<0) throw std::runtime_error("H264 open");
                    if (avcodec_parameters_from_context(vstream->codecpar,
                        vcodec)<0) throw std::runtime_error("video params");
                    vstream->time_base = vcodec->time_base;
                    const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
                    if (!ac) throw std::runtime_error("AAC unavailable");
                    astream = avformat_new_stream(fmt, nullptr);
                    if (!astream) throw std::runtime_error("audio stream");
                    acodec = avcodec_alloc_context3(ac);
                    if (!acodec) throw std::runtime_error("audio ctx");
                    acodec->sample_rate = c.audioSampleRate;
                    acodec->time_base = {1, c.audioSampleRate};
                    av_channel_layout_default(&acodec->ch_layout, c.audioChannels);
                    const AVSampleFormat* supported = nullptr;
#if LIBAVCODEC_VERSION_MAJOR >= 61
                    if (avcodec_get_supported_config(acodec, ac, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                    reinterpret_cast<const void**>(&supported), nullptr) < 0) supported = nullptr;
#else
                    supported = ac->sample_fmts;
#endif
                    acodec->sample_fmt = supported?supported[0]:AV_SAMPLE_FMT_FLTP;
                    acodec->bit_rate = c.audioBitrate>0?c.audioBitrate:96000;
                    if (fmt->oformat->flags&AVFMT_GLOBALHEADER)acodec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
                    if (avcodec_open2(acodec, ac, nullptr)<0)
                        throw std::runtime_error("AAC open");
                    if (avcodec_parameters_from_context(astream->codecpar,
                        acodec)<0) throw std::runtime_error("audio params");
                    astream->time_base = acodec->time_base;
                    AVChannelLayout inlayout{};
                    av_channel_layout_default(&inlayout, c.audioChannels);
                    if (swr_alloc_set_opts2(&swr, &acodec->ch_layout, acodec->sample_fmt,
                        acodec->sample_rate, &inlayout, AV_SAMPLE_FMT_FLT, c.audioSampleRate, 0,
                            nullptr)<0) throw std::runtime_error("swr opts");
                    av_channel_layout_uninit(&inlayout);
                    if (!swr || swr_init(swr)<0)
                        throw std::runtime_error("swr init");
                    sws = sws_getContext(c.outputWidth, c.outputHeight, AV_PIX_FMT_BGRA, c.outputWidth,
                        c.outputHeight, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws) throw std::runtime_error("sws init");
                    vframe = av_frame_alloc();
                    pkt = av_packet_alloc();
                    if (!vframe || !pkt) throw std::runtime_error("frames init");
                    vframe->format = vcodec->pix_fmt;
                    vframe->width = c.outputWidth;
                    vframe->height = c.outputHeight;
                    if (av_frame_get_buffer(vframe, 32)<0)
                        throw std::runtime_error("video buffer");
                    if (!(fmt->oformat->flags&AVFMT_NOFILE) && avio_open(&fmt->pb, filename.c_str(),
                        AVIO_FLAG_WRITE)<0) throw std::runtime_error("open temp file");
                    if (avformat_write_header(fmt, nullptr)<0)
                        throw std::runtime_error("mp4 header");
                }
                catch (...) {
                    release();
                    throw;
                }
            }
            int receive(AVCodecContext* ctx, AVStream* stream) {
                for (;;) {
                    int r = avcodec_receive_packet(ctx, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
                    if (r<0) return r;
                    av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
                    pkt->stream_index = stream->index;
                    pkt->pos = -1;
                    r = av_interleaved_write_frame(fmt, pkt);
                    av_packet_unref(pkt);
                    if (r<0) return r;
                }
            }
            int flush(AVCodecContext* ctx, AVStream* stream) {
                if (!ctx) return 0;
                int r = avcodec_send_frame(ctx, nullptr);
                if (r<0 && r != AVERROR_EOF) return r;
                return receive(ctx, stream);
            }
            int send(AVCodecContext* ctx, AVStream* stream, AVFrame* f) {
                int r = avcodec_send_frame(ctx, f);
                if (r<0) return r;
                return receive(ctx, stream);
            }
            int video(const FrameJob& j) {
                int64_t pts = av_rescale_q(j.pts, AVRational{1, 1000000}, vcodec->time_base);
                if (pts <= lastVideo) return 0;
                // duplicate frame slot
                lastVideo = pts;
                if (av_frame_make_writable(vframe)<0) return -1;
                const uint8_t* src[] = {j.bytes.data()};
                int lines[] = {j.stride};
                if (sws_scale(sws, src, lines, 0, j.h, vframe->data, vframe->linesize) <= 0)
                    return -1;
                vframe->pts = pts;
                return send(vcodec, vstream, vframe);
            }
            int encodeAudio(int n) {
                AVFrame* f = av_frame_alloc();
                if (!f) return -1;
                f->format = acodec->sample_fmt;
                f->sample_rate = acodec->sample_rate;
                f->nb_samples = n;
                if (av_channel_layout_copy(&f->ch_layout, &acodec->ch_layout)<0 || av_frame_get_buffer(f,
                    0)<0) {
                    av_frame_free(&f);
                    return -1;
                }
                const uint8_t* src[] = {reinterpret_cast<const uint8_t*>(pendingAudio.data()+pendingPos)};
                int got = swr_convert(swr, f->data, n, src, n);
                if (got<0) {
                    av_frame_free(&f);
                    return -1;
                }
                f->nb_samples = got;
                f->pts = audioSamples;
                audioSamples+=got;
                int r = send(acodec, astream, f);
                av_frame_free(&f);
                pendingPos+=size_t(n)*c.audioChannels;
                return r;
            }
            int audio(const FrameJob& j) {
                // Align audio to the common recording origin; fill missing intervals with silence.
                const int64_t wanted = av_rescale_q(j.pts, AVRational{1, 1000000}, AVRational{1,
                    c.audioSampleRate});
                int64_t cursor = audioSamples+int64_t((pendingAudio.size()-pendingPos)/c.audioChannels);
                int64_t gap = wanted-cursor;
                // Limit recovery to a short gap; large discontinuities should be fixed by the caller.
                if (gap>0 && gap <= int64_t(c.audioSampleRate)*5) {
                    pendingAudio.insert(pendingAudio.end(), size_t(gap)*c.audioChannels, 0.0f);
                    cursor+=gap;
                }
                if (gap>int64_t(c.audioSampleRate)*5) return -1;
                const int64_t skip = std::min<int64_t>(j.n, std::max<int64_t>(0, -gap));
                auto* ptr = reinterpret_cast<const float*>(j.bytes.data());
                pendingAudio.insert(pendingAudio.end(), ptr+size_t(skip)*j.ch, ptr+size_t(j.n)*j.ch);
                int frame = acodec->frame_size>0?acodec->frame_size:1024;
                while (pendingAudio.size()-pendingPos >= size_t(frame)*c.audioChannels) {
                    if (encodeAudio(frame)<0) return -1;
                }
                if (pendingPos>0) {
                    pendingAudio.erase(pendingAudio.begin(), pendingAudio.begin()+pendingPos);
                    pendingPos = 0;
                }
                return 0;
            }
            void run() {
                for (;;) {
                    FrameJob j;
                    {
                        std::unique_lock lk(mu);
                        cv.wait(lk, [&]{return stopping || !q.empty();});
                        if (q.empty()) {
                            if (stopping)break;
                            else continue;
                        }
                        j = std::move(q.front());
                        q.pop_front();
                        bytes-=j.bytes.size();
                    }
                    if ((j.type == static_cast<int>(FrameKind::Video)?video(j):audio(j))<0) {
                        failed = true;
                        break;
                    }
                }
                if (!failed && pendingAudio.size()>pendingPos) {
                    const int frame = acodec->frame_size>0?acodec->frame_size:1024;
                    const size_t needed = size_t(frame)*c.audioChannels;
                    size_t remaining = pendingAudio.size()-pendingPos;
                    pendingAudio.resize(pendingAudio.size()+needed-remaining, 0.0f);
                    if (encodeAudio(frame)<0)failed = true;
                }
            }
            void stop() {
                std::lock_guard lk(mu);
                stopping = true;
                cv.notify_all();
            }
            void release() {
                if (pkt)av_packet_free(&pkt);
                if (vframe)av_frame_free(&vframe);
                if (sws)sws_freeContext(sws), sws = nullptr;
                if (swr)swr_free(&swr);
                if (vcodec)avcodec_free_context(&vcodec);
                if (acodec)avcodec_free_context(&acodec);
                if (fmt) {
                    if (fmt->pb)avio_closep(&fmt->pb);
                    avformat_free_context(fmt);
                    fmt = nullptr;
                }
            }
        };
    }
    // anonymous namespace
    struct Mp4Recorder::Impl {
        RecorderConfig cfg;
        std::unique_ptr<Core> core;
        std::string error;
        uint64_t videoCount = 0;
        uint64_t audioFrameCount = 0;
        bool closed = false;
        enum class InputMode {
            Unset, Legacy, Timed};
            InputMode mode = InputMode::Unset;
            int64_t lastSubmittedVideoSlot = -1;
            int64_t lastSubmittedAudioUs = -1;
            explicit Impl(int w, int h, int fps, int bitrate) {
                cfg.outputWidth = w;
                cfg.outputHeight = h;
                cfg.fps = fps;
                cfg.videoBitrate = bitrate;
                if (w <= 0 || h <= 0 || (w%2) || (h%2) || w>8192 || h>8192 || fps <= 0 || fps>120
                    || bitrate <= 0)
                throw std::invalid_argument("output dimensions must be positive even numbers, FPS in 1..120, bitrate positive");
            }
            bool fail(std::string message) {
                error = std::move(message);
                return false;
            }
            // Aspect-fit with letterboxing, nearest-neighbor pixels (no channel reinterpretation).
            //std::vector<uint8_t> compose(const std::vector<std::vector<uint8_t>>& images, int iw, int ih) {
            //    const int W = cfg.outputWidth, H = cfg.outputHeight;
            //    std::vector<uint8_t> result(size_t(W)*H*4, 0);
            //    for (size_t i = 3;i<result.size();i+=4)result[i] = 255;
            //    if (images.empty()) return result;
            //    const int n = static_cast<int>(images.size());
            //    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
            //    const int rows = (n+cols-1)/cols;
            //    for (int k = 0;k<n;++k) {
            //        const int col = k%cols, row = k/cols;
            //        const int cellX = (col*W)/cols, cellXEnd = ((col+1)*W)/cols;
            //        const int cellY = (row*H)/rows, cellYEnd = ((row+1)*H)/rows;
            //        const int cellW = cellXEnd-cellX, cellH = cellYEnd-cellY;
            //        const double factor = std::min(double(cellW)/iw, double(cellH)/ih);
            //        const int dw = std::max(1, std::min(cellW, int(std::floor(iw*factor))));
            //        const int dh = std::max(1, std::min(cellH, int(std::floor(ih*factor))));
            //        const int x0 = cellX+(cellW-dw)/2, y0 = cellY+(cellH-dh)/2;
            //        const auto& src = images[size_t(k)];
            //        for (int y = 0;y<dh;++y) {
            //            const int sy = std::min(ih-1, int((double(y)+0.5)*ih/dh));
            //            for (int x = 0;x<dw;++x) {
            //                const int sx = std::min(iw-1, int((double(x)+0.5)*iw/dw));
            //                std::memcpy(result.data()+(size_t(y0+y)*W+(x0+x))*4,
            //                src.data()+(size_t(sy)*iw+sx)*4, 4);
            //            }
            //        }
            //    }
            //    return result;
            //}




            std::vector<uint8_t> compose(const std::vector<std::vector<uint8_t>>& images,
                int iw, int ih) const {
                const int W = cfg.outputWidth, H = cfg.outputHeight;
                std::vector<uint8_t> result(size_t(W) * H * 4, 0);
                for (size_t i = 3; i < result.size(); i += 4) result[i] = 255;
                if (images.empty()) return result;

                const int n = static_cast<int>(images.size());
                const int cols = static_cast<int>(std::ceil(std::sqrt(double(n))));
                const int rows = (n + cols - 1) / cols;
                for (int k = 0; k < n; ++k) {
                    const int col = k % cols, row = k / cols;
                    const int cellX = (col * W) / cols, cellXEnd = ((col + 1) * W) / cols;
                    const int cellY = (row * H) / rows, cellYEnd = ((row + 1) * H) / rows;
                    blitOne(result, W, H, images[size_t(k)].data(), iw, ih,
                        cellX, cellY, cellXEnd - cellX, cellYEnd - cellY);
                }
                return result;
            }

            void blitOne(std::vector<uint8_t>& dst, int W, int H,
                const uint8_t* src, int iw, int ih,
                int cellX, int cellY, int cellW, int cellH) const {
                const double factor = std::min(double(cellW) / iw, double(cellH) / ih);
                const int dw = std::max(1, std::min(cellW, int(std::floor(iw * factor))));
                const int dh = std::max(1, std::min(cellH, int(std::floor(ih * factor))));
                const int x0 = cellX + (cellW - dw) / 2;
                const int y0 = cellY + (cellH - dh) / 2;
                for (int y = 0; y < dh; ++y) {
                    const int sy = std::min(ih - 1, int((double(y) + 0.5) * ih / dh));
                    for (int x = 0; x < dw; ++x) {
                        const int sx = std::min(iw - 1, int((double(x) + 0.5) * iw / dw));
                        std::memcpy(dst.data() + (size_t(y0 + y) * W + (x0 + x)) * 4,
                            src + (size_t(sy) * iw + sx) * 4, 4);
                    }
                }
            }

            // DecodedFrame 版本：每张图用自己的 width/height
            std::vector<uint8_t> composeFrames(const std::vector<std::shared_ptr<ST::DecodedFrame>>& frames) const {
                const int W = cfg.outputWidth, H = cfg.outputHeight;
                std::vector<uint8_t> result(size_t(W) * H * 4, 0);
                for (size_t i = 3; i < result.size(); i += 4) result[i] = 255;
                if (frames.empty()) return result;

                const int n = static_cast<int>(frames.size());
                const int cols = static_cast<int>(std::ceil(std::sqrt(double(n))));
                const int rows = (n + cols - 1) / cols;

                for (int k = 0; k < n; ++k) {
                    const int col = k % cols, row = k / cols;
                    const int cellX = (col * W) / cols, cellXEnd = ((col + 1) * W) / cols;
                    const int cellY = (row * H) / rows, cellYEnd = ((row + 1) * H) / rows;
                    const auto& f = frames[size_t(k)];
                    blitOne(result, W, H, f->rgba.data(), f->width, f->height,
                        cellX, cellY, cellXEnd - cellX, cellYEnd - cellY);
                }
                return result;
            }
        };
        Mp4Recorder::Mp4Recorder(int w, int h, int fps, int bitrate):impl_(std::make_unique<Impl>(w, h, fps,
            bitrate)) {
        }
        Mp4Recorder::~Mp4Recorder() = default;
        const std::string& Mp4Recorder::lastError() const noexcept {return impl_->error;
    }
    bool Mp4Recorder::addFrame(const std::vector<std::vector<uint8_t>>& images, int iw, int ih,
    const std::vector<float>& audio, int channels, int sampleRate){
        auto& p = *impl_;
        if (p.closed) return p.fail("recording already finalized");
        if (p.mode == Impl::InputMode::Timed)
            return p.fail("cannot mix addFrame with addFrameTimed");
        if (channels<1 || channels>2 || sampleRate<8000 || sampleRate>192000)
            return p.fail("invalid audio channels/sampleRate");
        if (audio.size()%size_t(channels) != 0)
            return p.fail("interleaved audio sample count not divisible by channels");
        if (images.size()>25) return p.fail("at most 25 images per batch");
        if (!images.empty()) {
            if (iw <= 0 || ih <= 0 || iw>8192 || ih>8192 || size_t(iw)>SIZE_MAX/size_t(ih)/4)
            return p.fail("invalid image dimensions");
            const size_t expected = size_t(iw)*size_t(ih)*4;
            for (const auto& img:images)if (img.size() != expected) return p.fail("RGBA image buffer size mismatch");
        }
        if (!p.core) {
            p.cfg.audioChannels = channels;
            p.cfg.audioSampleRate = sampleRate;
            try {
                p.core = std::make_unique<Core>(p.cfg);
            }
            catch (const std::exception& e) {
                return p.fail(e.what());
            }
        }
        else if (p.cfg.audioChannels != channels || p.cfg.audioSampleRate != sampleRate)
        return p.fail("audio format changed; keep channels/sampleRate constant");
        // Each nonempty video batch = one frame. No wall-clock waiting or duplicated frame insertion.
        if (!images.empty()) {
            auto frame = p.compose(images, iw, ih);
            RawFrame job{};
            job.type = FrameKind::Video;
            job.data = frame.data();
            job.width = p.cfg.outputWidth;
            job.height = p.cfg.outputHeight;
            job.stride_bytes = job.width*4;
            job.pts_us = static_cast<int64_t>(p.videoCount*1000000ULL/p.cfg.fps);
            const int ret = p.core->add(job);
            if (ret != Result::Ok)
                return p.fail("video queue/encoder error: "+std::to_string(ret));
            ++p.videoCount;
        }
        // Keep audio independent of number of video frames and their cadence.
        // Split blocks to satisfy Core max 1 second per submission.
        const size_t totalFrames = audio.size()/size_t(channels);
        for (size_t off = 0;off<totalFrames;) {
            const size_t count = std::min<size_t>(size_t(sampleRate), totalFrames-off);
            RawFrame job{};
            job.type = FrameKind::Audio;
            job.data = audio.data()+off*size_t(channels);
            job.channels = channels;
            job.sample_rate = sampleRate;
            job.sample_frames = static_cast<int>(count);
            job.pts_us = static_cast<int64_t>(p.audioFrameCount*1000000ULL/sampleRate);
            const int ret = p.core->add(job);
            if (ret != Result::Ok)
                return p.fail("audio queue/encoder error: "+std::to_string(ret));
            off+=count;
            p.audioFrameCount+=count;
        }
        p.mode = Impl::InputMode::Legacy;
        p.error.clear();
        return true;
    }
bool Mp4Recorder::addFrameTimed(const std::vector<std::vector<uint8_t>>& images,
int iw, int ih, int64_t videoPtsUs,
const std::vector<float>& audio,
int channels, int sampleRate, int64_t audioPtsUs){
    auto& p = *impl_;
    if (p.closed) return p.fail("recording already finalized");
    if (p.mode == Impl::InputMode::Legacy)
        return p.fail("cannot mix addFrameTimed with addFrame");
    if (channels<1 || channels>2 || sampleRate<8000 || sampleRate>192000)
    return p.fail("invalid audio channels/sampleRate");
    if (audio.size()%size_t(channels) != 0)
        return p.fail("interleaved audio length not divisible by channels");
    if (images.size()>25) return p.fail("at most 25 images per batch");
    if (images.empty() ? videoPtsUs != -1 : videoPtsUs<0)
    return p.fail("video timestamp must be >=0 with images, otherwise -1");
    if (audio.empty() ? audioPtsUs != -1 : audioPtsUs<0)
    return p.fail("audio timestamp must be >=0 with audio, otherwise -1");
    if (images.empty() && audio.empty()) return p.fail("both image and audio are empty");
    int64_t slot = -1;
    if (!images.empty()) {
        if (iw <= 0 || ih <= 0 || iw>8192 || ih>8192 || size_t(iw)>SIZE_MAX/size_t(ih)/4)
        return p.fail("invalid image dimensions");
        const size_t expected = size_t(iw)*size_t(ih)*4;
        for (const auto& img:images)if (img.size() != expected) return p.fail("RGBA image buffer size mismatch");
        // Match the actual encoder timebase (1/fps), avoiding duplicate output PTS.
        slot = av_rescale_q(videoPtsUs, AVRational{1, 1000000}, AVRational{1, p.cfg.fps});
        if (slot <= p.lastSubmittedVideoSlot)
            return p.fail("video PTS must advance to a new frame slot");
    }
    if (!audio.empty() && audioPtsUs<p.lastSubmittedAudioUs)
    return p.fail("audio PTS moved backwards");
    if (!p.core) {
        p.cfg.audioChannels = channels;
        p.cfg.audioSampleRate = sampleRate;
        try {
            p.core = std::make_unique<Core>(p.cfg);
        }
        catch (const std::exception& e) {
            return p.fail(e.what());
        }
    }
    else if (p.cfg.audioChannels != channels || p.cfg.audioSampleRate != sampleRate)
    return p.fail("audio format changed; keep channels/sampleRate constant");
    if (!images.empty()) {
        auto frame = p.compose(images, iw, ih);
        RawFrame j{};
        j.type = FrameKind::Video;
        j.data = frame.data();
        j.width = p.cfg.outputWidth;
        j.height = p.cfg.outputHeight;
        j.stride_bytes = j.width*4;
        j.pts_us = videoPtsUs;
        int ret = p.core->add(j);
        if (ret != Result::Ok)
            return p.fail("video queue/encoder error: "+std::to_string(ret));
        p.lastSubmittedVideoSlot = slot;
    }
    const size_t frames = audio.size()/size_t(channels);
    for (size_t off = 0;off<frames;) {
        const size_t count = std::min<size_t>(size_t(sampleRate), frames-off);
        RawFrame j{};
        j.type = FrameKind::Audio;
        j.data = audio.data()+off*size_t(channels);
        j.channels = channels;
        j.sample_rate = sampleRate;
        j.sample_frames = static_cast<int>(count);
        j.pts_us = audioPtsUs + static_cast<int64_t>(off*1000000ULL/sampleRate);
        int ret = p.core->add(j);
        if (ret != Result::Ok)
            return p.fail("audio queue/encoder error: "+std::to_string(ret));
        off+=count;
    }
    if (!audio.empty())p.lastSubmittedAudioUs = audioPtsUs;
    p.mode = Impl::InputMode::Timed;
    p.error.clear();
    return true;
}

bool Mp4Recorder::addFrames(const std::vector<std::shared_ptr<ST::DecodedFrame>>& frames,
    const std::vector<float>& audio,
    int channels, int sampleRate) {
    auto& p = *impl_;
    if (p.closed) return p.fail("recording already finalized");
    if (p.mode == Impl::InputMode::Timed)
        return p.fail("cannot mix addFrame/addFrames with addFrameTimed");

    if (channels < 1 || channels > 2 || sampleRate < 8000 || sampleRate > 192000)
        return p.fail("invalid audio channels/sampleRate");
    if (audio.size() % size_t(channels) != 0)
        return p.fail("interleaved audio sample count not divisible by channels");
    if (frames.size() > 25)
        return p.fail("at most 25 images per batch");

    if (!frames.empty()) {
        for (const auto& f0 : frames) {
            auto& f = *f0;
            if (f.width <= 0 || f.height <= 0 || f.width > 8192 || f.height > 8192 ||
                size_t(f.width) > SIZE_MAX / size_t(f.height) / 4 ||
                f.rgba.size() != size_t(f.width) * size_t(f.height) * 4)
                return p.fail("invalid DecodedFrame dimensions or RGBA buffer size");
        }
    }

    if (!p.core) {
        p.cfg.audioChannels = channels;
        p.cfg.audioSampleRate = sampleRate;
        try { p.core = std::make_unique<Core>(p.cfg); }
        catch (const std::exception& e) { return p.fail(e.what()); }
    }
    else if (p.cfg.audioChannels != channels || p.cfg.audioSampleRate != sampleRate)
        return p.fail("audio format changed; keep channels/sampleRate constant");

    if (!frames.empty()) {
        auto composed = p.composeFrames(frames);   // ← 混合尺寸也支持
        RawFrame job{};
        job.type = FrameKind::Video;
        job.data = composed.data();
        job.width = p.cfg.outputWidth;
        job.height = p.cfg.outputHeight;
        job.stride_bytes = job.width * 4;
        job.pts_us = static_cast<int64_t>(p.videoCount * 1000000ULL / p.cfg.fps);
        const int ret = p.core->add(job);
        if (ret != Result::Ok)
            return p.fail("video queue/encoder error: " + std::to_string(ret));
        ++p.videoCount;
    }

    const size_t totalFrames = audio.size() / size_t(channels);
    for (size_t off = 0; off < totalFrames;) {
        const size_t count = std::min<size_t>(size_t(sampleRate), totalFrames - off);
        RawFrame job{};
        job.type = FrameKind::Audio;
        job.data = audio.data() + off * size_t(channels);
        job.channels = channels;
        job.sample_rate = sampleRate;
        job.sample_frames = static_cast<int>(count);
        job.pts_us = static_cast<int64_t>(p.audioFrameCount * 1000000ULL / sampleRate);
        const int ret = p.core->add(job);
        if (ret != Result::Ok)
            return p.fail("audio queue/encoder error: " + std::to_string(ret));
        off += count;
        p.audioFrameCount += count;
    }

    p.mode = Impl::InputMode::Legacy;
    p.error.clear();
    return true;
}

bool Mp4Recorder::addFramesTimed(const std::vector<std::shared_ptr<ST::DecodedFrame>>& frames,
    int64_t videoPtsUs,
    const std::vector<float>& audio,
    int channels, int sampleRate, int64_t audioPtsUs) {
    auto& p = *impl_;
    if (p.closed) return p.fail("recording already finalized");
    if (p.mode == Impl::InputMode::Legacy)
        return p.fail("cannot mix addFrameTimed with addFrame/addFrames");

    if (channels < 1 || channels > 2 || sampleRate < 8000 || sampleRate > 192000)
        return p.fail("invalid audio channels/sampleRate");
    if (audio.size() % size_t(channels) != 0)
        return p.fail("interleaved audio length not divisible by channels");
    if (frames.size() > 25) return p.fail("at most 25 images per batch");

    if (frames.empty() ? videoPtsUs != -1 : videoPtsUs < 0)
        return p.fail("video timestamp must be >=0 with images, otherwise -1");
    if (audio.empty() ? audioPtsUs != -1 : audioPtsUs < 0)
        return p.fail("audio timestamp must be >=0 with audio, otherwise -1");
    if (frames.empty() && audio.empty())
        return p.fail("both image and audio are empty");

    int64_t slot = -1;
    if (!frames.empty()) {
        for (const auto& f0 : frames) {
            auto& f = *f0;
            if (f.width <= 0 || f.height <= 0 || f.width > 8192 || f.height > 8192 ||
                size_t(f.width) > SIZE_MAX / size_t(f.height) / 4 ||
                f.rgba.size() != size_t(f.width) * size_t(f.height) * 4)
                return p.fail("invalid DecodedFrame dimensions or RGBA buffer size");
        }
        slot = av_rescale_q(videoPtsUs, AVRational{ 1, 1000000 }, AVRational{ 1, p.cfg.fps });
        if (slot <= p.lastSubmittedVideoSlot)
            return p.fail("video PTS must advance to a new frame slot");
    }
    if (!audio.empty() && audioPtsUs < p.lastSubmittedAudioUs)
        return p.fail("audio PTS moved backwards");

    if (!p.core) {
        p.cfg.audioChannels = channels;
        p.cfg.audioSampleRate = sampleRate;
        try { p.core = std::make_unique<Core>(p.cfg); }
        catch (const std::exception& e) { return p.fail(e.what()); }
    }
    else if (p.cfg.audioChannels != channels || p.cfg.audioSampleRate != sampleRate)
        return p.fail("audio format changed; keep channels/sampleRate constant");

    if (!frames.empty()) {
        auto composed = p.composeFrames(frames);
        RawFrame j{};
        j.type = FrameKind::Video;
        j.data = composed.data();
        j.width = p.cfg.outputWidth;
        j.height = p.cfg.outputHeight;
        j.stride_bytes = j.width * 4;
        j.pts_us = videoPtsUs;
        int ret = p.core->add(j);
        if (ret != Result::Ok)
            return p.fail("video queue/encoder error: " + std::to_string(ret));
        p.lastSubmittedVideoSlot = slot;
    }

    const size_t frameCount = audio.size() / size_t(channels);
    for (size_t off = 0; off < frameCount;) {
        const size_t count = std::min<size_t>(size_t(sampleRate), frameCount - off);
        RawFrame j{};
        j.type = FrameKind::Audio;
        j.data = audio.data() + off * size_t(channels);
        j.channels = channels;
        j.sample_rate = sampleRate;
        j.sample_frames = static_cast<int>(count);
        j.pts_us = audioPtsUs + static_cast<int64_t>(off * 1000000ULL / sampleRate);
        int ret = p.core->add(j);
        if (ret != Result::Ok)
            return p.fail("audio queue/encoder error: " + std::to_string(ret));
        off += count;
    }
    if (!audio.empty()) p.lastSubmittedAudioUs = audioPtsUs;

    p.mode = Impl::InputMode::Timed;
    p.error.clear();
    return true;
}


bool Mp4Recorder::save(const std::filesystem::path& path) {
    auto& p = *impl_;
    if (p.closed) return p.fail("save() already called");
    if (!p.core) return p.fail("no input frames supplied");

    p.closed = true;                              // save 期间禁止重入
    const int code = p.core->save(path);

    // ★ 重置所有状态，允许下一次录制
    p.core.reset();                               // Core::save 已 release，析构安全
    p.closed = false;
    p.videoCount = 0;
    p.audioFrameCount = 0;
    p.mode = Impl::InputMode::Unset;
    p.lastSubmittedVideoSlot = -1;
    p.lastSubmittedAudioUs = -1;

    if (code != Result::Ok)
        return p.fail("finalization failed (code " + std::to_string(code) + ")");
    p.error.clear();
    return true;
}

int Mp4Recorder::videoFps() {
    assert(impl_);
    return impl_->cfg.fps;
}

}
// namespace recording
