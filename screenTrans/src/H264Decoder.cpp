#include "H264Decoder.h"
#include <iostream>

namespace ST {

    H264Decoder::H264Decoder()
    {
        if(!Initialize()){
            std::cerr << "H264Dncoder init error" << std::endl;
        }
    }

    bool H264Decoder::Initialize()
    {
        Destroy();

        const AVCodec* codec =
            avcodec_find_decoder(
                AV_CODEC_ID_H264
            );

        if (!codec)
            return false;

        m_ctx =
            avcodec_alloc_context3(codec);

        if (!m_ctx)
            return false;

        if (avcodec_open2(m_ctx, codec, nullptr) < 0)
        {
            Destroy();
            return false;
        }

        m_frame = av_frame_alloc();

        if (!m_frame)
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

        return true;
    }

    std::vector<DecodedFrame>
        H264Decoder::DecodePacket(
            const std::vector<uint8_t>& packet)
    {
        std::vector<DecodedFrame> frames;

        if (!m_ctx)
            return frames;

        if (packet.empty())
            return frames;

        av_packet_unref(m_pkt);

        m_pkt->data =
            const_cast<uint8_t*>(packet.data());

        m_pkt->size =
            (int)packet.size();

        if (avcodec_send_packet(m_ctx, m_pkt) < 0)
            return frames;

        while (true)
        {
            int ret =
                avcodec_receive_frame(
                    m_ctx,
                    m_frame
                );

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            int width = m_frame->width;
            int height = m_frame->height;

            auto fmt =
                (AVPixelFormat)m_frame->format;

            if (!m_sws ||
                width != m_last_width ||
                height != m_last_height ||
                fmt != m_last_fmt)
            {
                if (m_sws)
                    sws_freeContext(m_sws);

                m_sws =
                    sws_getContext(
                        width,
                        height,
                        fmt,

                        width,
                        height,
                        AV_PIX_FMT_RGBA,

                        SWS_FAST_BILINEAR,
                        nullptr,
                        nullptr,
                        nullptr
                    );

                m_last_width = width;
                m_last_height = height;
                m_last_fmt = fmt;
            }

            DecodedFrame out;

            out.width = width;
            out.height = height;

            out.rgba.resize(
                width * height * 4
            );

            uint8_t* dst[1] = {
                out.rgba.data()
            };

            int stride[1] = {
                width * 4
            };

            sws_scale(
                m_sws,
                m_frame->data,
                m_frame->linesize,
                0,
                height,
                dst,
                stride
            );

            frames.push_back(
                std::move(out)
            );
        }

        return frames;
    }

    std::vector<DecodedFrame>
        H264Decoder::Flush()
    {
        std::vector<DecodedFrame> frames;

        if (!m_ctx)
            return frames;

        avcodec_send_packet(m_ctx, nullptr);

        while (true)
        {
            int ret =
                avcodec_receive_frame(
                    m_ctx,
                    m_frame
                );

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;
        }

        return frames;
    }

    void H264Decoder::Destroy()
    {
        if (m_sws)
        {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }

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

        if (m_ctx)
        {
            avcodec_free_context(&m_ctx);
            m_ctx = nullptr;
        }

        m_last_width = 0;
        m_last_height = 0;
        m_last_fmt = AV_PIX_FMT_NONE;
    }

}