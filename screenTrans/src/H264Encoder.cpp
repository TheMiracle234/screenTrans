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

#include "H264Encoder.h"

#include <iostream>
#include <array>

namespace ST {

    H264Encoder::H264Encoder(int width, int height, int fps, int bitrate) {
        if (!Initialize(width, height, fps, bitrate)) { 
            std::cerr << "H264Encoder init error" << std::endl; 
        }
    }

    const AVCodec* H264Encoder::SelectEncoder(std::string& out_name, bool& out_hw)
    {
        static const std::array<const char*, 4> encoders = {
            "libx264",
            "h264_nvenc",
            "h264_amf",
            "h264_qsv",
        };

        for (auto name : encoders)
        {
            const AVCodec* codec =
                avcodec_find_encoder_by_name(name);

            if (!codec)
                continue;

            AVCodecContext* ctx =
                avcodec_alloc_context3(codec);

            if (!ctx)
                continue;

            ctx->width = 1920;
            ctx->height = 1080;
            ctx->time_base = { 1,60 };
            ctx->framerate = { 60,1 };
            ctx->gop_size = 12;
            ctx->max_b_frames = 0;
            ctx->bit_rate = 4000000;

            if (std::string(name) == "h264_qsv")
                ctx->pix_fmt = AV_PIX_FMT_NV12;
            else if (std::string(name) == "h264_nvenc")
                ctx->pix_fmt = AV_PIX_FMT_NV12;
            else if (std::string(name) == "h264_amf")
                ctx->pix_fmt = AV_PIX_FMT_NV12;
            else
                ctx->pix_fmt = AV_PIX_FMT_YUV420P;

            AVDictionary* opts = nullptr;

            if (std::string(name) == "libx264")
            {
                av_dict_set(&opts, "preset", "ultrafast", 0);
                av_dict_set(&opts, "tune", "zerolatency", 0);
            }
            else if (std::string(name) == "h264_nvenc")
            {
                av_dict_set(&opts, "preset", "p1", 0);
                av_dict_set(&opts, "tune", "ll", 0);
            }

            int ret =
                avcodec_open2(ctx, codec, &opts);

            av_dict_free(&opts);

            avcodec_free_context(&ctx);

            if (ret >= 0)
            {
                out_name = name;
                out_hw = (std::string(name) != "libx264");

                std::cout
                    << "Selected encoder: "
                    << name
                    << " (hardware="
                    << out_hw
                    << ")\n";

                return codec;
            }
        }

        return nullptr;
    }

    bool H264Encoder::Reset(int width, int height, int fps, int bitrate) {
        Destroy();
        return Initialize(
            width == NO_CHANGE ? m_width : width, 
            height == NO_CHANGE ? m_height : height, 
            fps == NO_CHANGE ? m_fps : fps, 
            bitrate == NO_CHANGE ? m_bitrate : bitrate
        );
    }

    bool H264Encoder::Initialize(
        int width,
        int height,
        int fps,
        int bitrate)
    {
        Destroy();

        m_width = width;
        m_height = height;
        m_fps = fps;
        m_bitrate = bitrate;

        const AVCodec* codec =
            SelectEncoder(
                m_encoder_name,
                m_is_hw
            );

        if (!codec)
            return false;

        m_ctx =
            avcodec_alloc_context3(codec);

        if (!m_ctx)
            return false;

        m_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
        m_ctx->codec_id = AV_CODEC_ID_H264;

        m_ctx->width = width;
        m_ctx->height = height;

        m_ctx->time_base = { 1,fps };
        m_ctx->framerate = { fps,1 };

        m_ctx->gop_size = 12;
        m_ctx->max_b_frames = 0;

        m_ctx->bit_rate = bitrate;

        if (m_is_hw)
            m_ctx->pix_fmt = AV_PIX_FMT_NV12;
        else
            m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

        AVDictionary* opts = nullptr;

        if (m_encoder_name == "libx264")
        {
            av_dict_set(&opts, "preset", "ultrafast", 0);
            av_dict_set(&opts, "tune", "zerolatency", 0);
        }
        else if (m_encoder_name == "h264_nvenc")
        {
            av_dict_set(&opts, "preset", "p1", 0);
            av_dict_set(&opts, "tune", "ll", 0);
        }

        av_dict_set(&opts, "repeat_headers", "1", 0);

        if (avcodec_open2(m_ctx, codec, &opts) < 0)
        {
            av_dict_free(&opts);
            Destroy();
            return false;
        }

        av_dict_free(&opts);

        m_frame = av_frame_alloc();

        if (!m_frame)
        {
            Destroy();
            return false;
        }

        m_frame->format = m_ctx->pix_fmt;
        m_frame->width = width;
        m_frame->height = height;

        if (av_frame_get_buffer(m_frame, 32) < 0)
        {
            Destroy();
            return false;
        }

        m_pkt = av_packet_alloc();

        if (!m_pkt)
        {
            Destroy();
            return false;
        }

        m_sws =
            sws_getContext(
                width,
                height,
                AV_PIX_FMT_RGBA,

                width,
                height,
                m_ctx->pix_fmt,

                SWS_FAST_BILINEAR,
                nullptr,
                nullptr,
                nullptr
            );

        if (!m_sws)
        {
            Destroy();
            return false;
        }

        return true;
    }

    std::vector<std::vector<uint8_t>>
        H264Encoder::EncodeFrame(
            const std::vector<uint8_t>& rgba)
    {
        std::vector<std::vector<uint8_t>> packets;

        if (!m_ctx)
            return packets;

        if (rgba.empty())
            return packets;

        av_frame_make_writable(m_frame);

        const uint8_t* src[1] = {
            rgba.data()
        };

        int stride[1] = {
            m_width * 4
        };

        sws_scale(
            m_sws,
            src,
            stride,
            0,
            m_height,
            m_frame->data,
            m_frame->linesize
        );

        m_frame->pts = m_pts++;

        if (avcodec_send_frame(m_ctx, m_frame) < 0)
            return packets;

        while (true)
        {
            int ret =
                avcodec_receive_packet(
                    m_ctx,
                    m_pkt
                );

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            packets.emplace_back(
                m_pkt->data,
                m_pkt->data + m_pkt->size
            );

            av_packet_unref(m_pkt);
        }

        return packets;
    }

    std::vector<std::vector<uint8_t>>
        H264Encoder::Flush()
    {
        std::vector<std::vector<uint8_t>> packets;

        if (!m_ctx)
            return packets;

        avcodec_send_frame(m_ctx, nullptr);

        while (true)
        {
            int ret =
                avcodec_receive_packet(
                    m_ctx,
                    m_pkt
                );

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            packets.emplace_back(
                m_pkt->data,
                m_pkt->data + m_pkt->size
            );

            av_packet_unref(m_pkt);
        }

        return packets;
    }

    void H264Encoder::Destroy()
    {
        if (m_pkt)
        {
            av_packet_free(&m_pkt);
            m_pkt = nullptr;
        }

        if (m_frame)
        {
            av_frame_free(&m_frame);
            m_frame = nullptr;
        }

        if (m_sws)
        {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }

        if (m_ctx)
        {
            avcodec_free_context(&m_ctx);
            m_ctx = nullptr;
        }

        m_pts = 0;
    }

}