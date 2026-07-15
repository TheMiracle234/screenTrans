#include "st_util.h"

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <execution>
#include <array>
#include <thread>

extern "C" {
#	include <libavcodec/avcodec.h>
#	include <libavutil/opt.h>
#	include <libavutil/imgutils.h>
#	include <libswscale/swscale.h>
}

namespace ST {

#if 0

	// one thread
	pos_change choose(const std::vector<uint8_t>& last, const std::vector<uint8_t>& now, uint8_t err) {
		pos_change res;
		res.idx.reserve(now.size());
		res.data.reserve(now.size());
		for (int i = 0;i < last.size();++i) {
			if (std::abs(last[i] - now[i]) < err) {
				continue;
			}
			res.idx.push_back(i);
			res.data.push_back(now[i]);
		}
		return res;
	}

	void updateLastData(std::vector<uint8_t>& last, const pos_change& pd) {
		for (int i = 0;i < pd.idx.size();++i) {
			last[pd.idx[i]] = pd.data[i];
		}
	}

#elif 1

	// multi threads with mutex
	pos_change choose(const std::vector<uint8_t>& last, const std::vector<uint8_t>& now, uint8_t err) {
		pos_change res;
		res.idx.reserve(now.size());
		res.data.reserve(now.size());

		std::vector<int> out_indices(CORES + 1);
		int step = (int)now.size() / CORES;
		for (int i = 0;i < CORES + 1;++i) {
			out_indices[i] = step * i;
		}

		static std::mutex mut;

		std::for_each(std::execution::par, out_indices.begin(), out_indices.end(), [&, step](int i) {
			int end = std::min(i + step, (int)now.size());
			for (int j = i;j < end; ++j) {
				if (std::abs(last[j] - now[j]) <= err)
					continue;
				mut.lock();
				res.idx.push_back(j);
				res.data.push_back(now[j]);
				mut.unlock();
			}
		});

		return res;
	}

	void updateLastData(std::vector<uint8_t>& last, const pos_change& pd) {
		for (int i = 0;i < pd.idx.size();++i) {
			last[pd.idx[i]] = pd.data[i];
		}
	}

#elif 0

	// multi threads without mutex
	pos_change choose(const std::vector<uint8_t>& last, const std::vector<uint8_t>& now, uint8_t err) {
		pos_change res;
		res.idx.reserve(now.size());
		res.data.reserve(now.size());

		std::vector<int> out_indices(CORES + 1);
		int step = (int)now.size() / CORES;
		for (int i = 0;i < CORES + 1;++i) {
			out_indices[i] = step * i;
		}

		std::vector<std::vector<int>> part_idx(CORES + 1);
		std::vector<std::vector<uint8_t>> part_data(CORES + 1);

		std::for_each(std::execution::par, out_indices.begin(), out_indices.end(), [&, step](int i) {
			auto& idx = part_idx[i / step];
			idx.reserve(step);
			auto& data = part_data[i / step];
			data.reserve(step);
			int end = std::min(i + step, (int)now.size());
			for (int j = i;j < end; ++j) {
				if (std::abs(last[j] - now[j]) <= err)
					continue;
				idx.push_back(j);
				data.push_back(now[j]);
			}
		});

		for (int i = 0; i < CORES + 1; ++i) {
			res.idx.insert(res.idx.end(), part_idx[i].begin(), part_idx[i].end());
			res.data.insert(res.data.end(), part_data[i].begin(), part_data[i].end());
		}

		return res;
	}

	void updateLastData(std::vector<uint8_t>& last, const pos_change& pd) {
		for (int i = 0;i < pd.idx.size();++i) {
			last[pd.idx[i]] = pd.data[i];
		}
	}

#endif

#if 1

    /////////////////////////////////////////////////////////////
        // encoder 选择结果
        /////////////////////////////////////////////////////////////
    struct EncoderCtx
    {
        const AVCodec* codec = nullptr;
        std::string name;
    };

    /////////////////////////////////////////////////////////////
    // 只做一次：选择 encoder（工业级）
    /////////////////////////////////////////////////////////////
    static EncoderCtx select_encoder(int w, int h, int fps)
    {
        static const std::array<const char*, 4> encoders = {
            "libx264",
            "h264_nvenc",
            "h264_qsv",
            "h264_amf",
        };

        for (auto name : encoders)
        {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (!codec)
                continue;

            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            if (!ctx)
                continue;

            ctx->width = w;
            ctx->height = h;
            ctx->time_base = { 1, fps };
            ctx->framerate = { fps, 1 };
            ctx->bit_rate = 2'000'000;
            ctx->gop_size = 1;
            ctx->max_b_frames = 0;

            // pix_fmt 关键修复
            if (std::string(name).find("qsv") != std::string::npos)
                ctx->pix_fmt = AV_PIX_FMT_NV12;
            else
                ctx->pix_fmt = AV_PIX_FMT_YUV420P;

            AVDictionary* opt = nullptr;

            if (std::string(name) == "libx264")
            {
                av_dict_set(&opt, "preset", "ultrafast", 0);
                av_dict_set(&opt, "tune", "zerolatency", 0);
            }
            else if (std::string(name).find("nvenc") != std::string::npos)
            {
                av_dict_set(&opt, "preset", "p1", 0);
                av_dict_set(&opt, "tune", "ll", 0);
            }

            if (avcodec_open2(ctx, codec, &opt) == 0)
            {
                av_dict_free(&opt);
                avcodec_free_context(&ctx);

                return { codec, name };
            }

            av_dict_free(&opt);
            avcodec_free_context(&ctx);
        }

        return {};
    }

    /////////////////////////////////////////////////////////////
    // static encoder（只初始化一次）
    /////////////////////////////////////////////////////////////
    static EncoderCtx g_encoder;

    /////////////////////////////////////////////////////////////
    // RGBA -> H264
    /////////////////////////////////////////////////////////////
    std::vector<uint8_t> compress_bytes_to_h264_auto(
        const std::vector<uint8_t>& rgba,
        int width, int height,
        int fps, int bitrate)
    {
        av_log_set_level(AV_LOG_ERROR);

        std::vector<uint8_t> output;

        if (rgba.empty())
            return {};

        /////////////////////////////////////////////////////////
        // 只选一次 encoder
        /////////////////////////////////////////////////////////
        if (!g_encoder.codec)
        {
            g_encoder = select_encoder(width, height, fps);

            if (!g_encoder.codec)
            {
                std::cerr << "No encoder available\n";
                return {};
            }

            std::cout << "Selected encoder: " << g_encoder.name << "\n";
        }

        const AVCodec* codec = g_encoder.codec;

        /////////////////////////////////////////////////////////
        // context（每帧创建）
        /////////////////////////////////////////////////////////
        AVCodecContext* cctx = avcodec_alloc_context3(codec);

        cctx->width = width;
        cctx->height = height;
        cctx->time_base = { 1, fps };
        cctx->framerate = { fps, 1 };
        cctx->bit_rate = bitrate;
        cctx->gop_size = 1;
        cctx->max_b_frames = 0;

        if (g_encoder.name.find("qsv") != std::string::npos)
            cctx->pix_fmt = AV_PIX_FMT_NV12;
        else
            cctx->pix_fmt = AV_PIX_FMT_YUV420P;

        AVDictionary* opt = nullptr;

        if (g_encoder.name == "libx264")
        {
            av_dict_set(&opt, "preset", "ultrafast", 0);
            av_dict_set(&opt, "tune", "zerolatency", 0);
        }
        else if (g_encoder.name.find("nvenc") != std::string::npos)
        {
            av_dict_set(&opt, "preset", "p1", 0);
        }

        if (avcodec_open2(cctx, codec, &opt) < 0)
        {
            av_dict_free(&opt);
            avcodec_free_context(&cctx);
            return {};
        }

        av_dict_free(&opt);

        /////////////////////////////////////////////////////////
        // frame + sws
        /////////////////////////////////////////////////////////
        AVFrame* frame = av_frame_alloc();
        frame->format = cctx->pix_fmt;
        frame->width = width;
        frame->height = height;

        av_frame_get_buffer(frame, 32);
        av_frame_make_writable(frame);

        SwsContext* sws = sws_getContext(
            width, height, AV_PIX_FMT_RGBA,
            width, height, cctx->pix_fmt,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr);

        const uint8_t* src[1] = { rgba.data() };
        int stride[1] = { width * 4 };

        sws_scale(sws, src, stride, 0, height,
            frame->data, frame->linesize);

        frame->pts = 0;

        /////////////////////////////////////////////////////////
        // encode
        /////////////////////////////////////////////////////////
        AVPacket* pkt = av_packet_alloc();

        if (avcodec_send_frame(cctx, frame) >= 0)
        {
            while (true)
            {
                int ret = avcodec_receive_packet(cctx, pkt);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;

                if (ret < 0)
                    break;

                output.insert(output.end(),
                    pkt->data,
                    pkt->data + pkt->size);

                av_packet_unref(pkt);
            }
        }

        avcodec_send_frame(cctx, nullptr);

        while (true)
        {
            int ret = avcodec_receive_packet(cctx, pkt);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            output.insert(output.end(),
                pkt->data,
                pkt->data + pkt->size);

            av_packet_unref(pkt);
        }

        /////////////////////////////////////////////////////////
        // cleanup
        /////////////////////////////////////////////////////////
        av_packet_free(&pkt);
        sws_freeContext(sws);
        av_frame_free(&frame);
        avcodec_free_context(&cctx);

        return output;
    }

    /////////////////////////////////////////////////////////////
    // decoder（static，只初始化一次）
    /////////////////////////////////////////////////////////////
    static const AVCodec* g_decoder =
        avcodec_find_decoder(AV_CODEC_ID_H264);

    static AVCodecContext* g_dctx = nullptr;

    /////////////////////////////////////////////////////////////
    // H264 -> RGBA
    /////////////////////////////////////////////////////////////
    std::vector<uint8_t> decompress_h264_to_bytes(
        const std::vector<uint8_t>& h264,
        int& out_width,
        int& out_height)
    {
        out_width = out_height = 0;

        if (h264.empty())
            return {};

        if (!g_decoder)
            return {};

        if (!g_dctx)
        {
            g_dctx = avcodec_alloc_context3(g_decoder);
            avcodec_open2(g_dctx, g_decoder, nullptr);
        }

        AVPacket *pkt;
        pkt = av_packet_alloc();
        pkt->data = (uint8_t*)h264.data();
        pkt->size = (int)h264.size();

        if (avcodec_send_packet(g_dctx, pkt) < 0)
            return {};

        AVFrame* frame = av_frame_alloc();

        int ret = avcodec_receive_frame(g_dctx, frame);
        if (ret < 0)
        {
            av_frame_free(&frame);
            return {};
        }

        out_width = frame->width;
        out_height = frame->height;

        std::vector<uint8_t> rgba(out_width * out_height * 4);

        SwsContext* sws = sws_getContext(
            out_width, out_height, (AVPixelFormat)frame->format,
            out_width, out_height, AV_PIX_FMT_RGBA,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr);

        uint8_t* dst[1] = { rgba.data() };
        int stride[1] = { out_width * 4 };

        sws_scale(sws,
            frame->data,
            frame->linesize,
            0,
            out_height,
            dst,
            stride);

        sws_freeContext(sws);
        av_frame_free(&frame);

        return rgba;
    }

#endif

}