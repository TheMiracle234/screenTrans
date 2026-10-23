#pragma once
//#include <iostream>
#include <vector>
#include <cstdint>
//#include <cstring>
#include <cmath>
#include <cassert>

namespace audio {

    class Compressor {
    private:
        uint32_t sampleRate_;
        uint16_t channels_;

        // float [-1, 1] -> int16
        static int16_t floatToInt16(float value) {
            if (value > 1.0f)
                value = 1.0f;
            if (value < -1.0f)
                value = -1.0f;

            return static_cast<int16_t>(
                round(value * 32767.0f)
                );
        }

        // int16 -> float [-1, 1]
        static float int16ToFloat(int16_t value) {
            return static_cast<float>(value) / 32767.0f;
        }

        // ZigZag encode
        static uint32_t zigzagEncode(int32_t value) {
            return (static_cast<uint32_t>(value) << 1)
                ^ static_cast<uint32_t>(value >> 31);
        }

        // ZigZag decode
        static int32_t zigzagDecode(uint32_t value) {
            uint32_t mask = 0u - (value & 1u);

            return static_cast<int32_t>(
                (value >> 1) ^ mask
                );
        }

        // 写入 Varint
        static void writeVarint(
            std::vector<uint8_t>& output,
            uint32_t value
        ) {
            while (value >= 128) {
                output.push_back(
                    static_cast<uint8_t>((value & 0x7F) | 0x80)
                );

                value >>= 7;
            }

            output.push_back(
                static_cast<uint8_t>(value)
            );
        }

        // 从 Varint 读取一个整数
        static uint32_t readVarint(
            const std::vector<uint8_t>& data,
            size_t& position
        ) {
            uint32_t result = 0;
            int shift = 0;

            while (true) {
                assert(position < data.size());

                uint8_t byte = data[position++];

                result |=
                    static_cast<uint32_t>(byte & 0x7F) << shift;

                if ((byte & 0x80) == 0)
                    break;

                shift += 7;

                assert(shift <= 28);
            }

            return result;
        }

        // 写入 32 位整数
        static void writeUint32(
            std::vector<uint8_t>& output,
            uint32_t value
        ) {
            output.push_back(static_cast<uint8_t>(value));
            output.push_back(static_cast<uint8_t>(value >> 8));
            output.push_back(static_cast<uint8_t>(value >> 16));
            output.push_back(static_cast<uint8_t>(value >> 24));
        }

        // 写入 16 位整数
        static void writeUint16(
            std::vector<uint8_t>& output,
            uint16_t value
        ) {
            output.push_back(static_cast<uint8_t>(value));
            output.push_back(static_cast<uint8_t>(value >> 8));
        }

        // 读取 32 位整数
        static uint32_t readUint32(
            const std::vector<uint8_t>& data,
            size_t& position
        ) {
            assert(position + 4 <= data.size());

            uint32_t value =
                static_cast<uint32_t>(data[position])
                | (static_cast<uint32_t>(data[position + 1]) << 8)
                | (static_cast<uint32_t>(data[position + 2]) << 16)
                | (static_cast<uint32_t>(data[position + 3]) << 24);

            position += 4;
            return value;
        }

        // 读取 16 位整数
        static uint16_t readUint16(
            const std::vector<uint8_t>& data,
            size_t& position
        ) {
            assert(position + 2 <= data.size());

            uint16_t value =
                static_cast<uint16_t>(data[position])
                | (static_cast<uint16_t>(data[position + 1]) << 8);

            position += 2;
            return value;
        }

    public:
        Compressor(
            uint32_t sampleRate,
            uint16_t channels
        )
            : sampleRate_(sampleRate),
            channels_(channels)
        {
            assert(sampleRate_ != 0);
            assert(channels_ != 0);
        }

        // 压缩
        void compress( std::vector<uint8_t>& output, const std::vector<float>& audioData ) const {
            assert(audioData.size() % channels_ == 0);

            /*
                Header:

                4 bytes: Magic "AC01"
                4 bytes: sampleRate
                2 bytes: channels
                4 bytes: sampleCount
            */

            output.resize(0);

            // 预留空间，减少 vector 扩容
            output.reserve(14 + audioData.size() * 2);

            // Magic
            output.push_back('A');
            output.push_back('C');
            output.push_back('0');
            output.push_back('1');

            // 音频参数
            writeUint32(output, sampleRate_);
            writeUint16(output, channels_);

            // 原始 sample 数量
            writeUint32(
                output,
                static_cast<uint32_t>(audioData.size())
            );

            /*
                对每一个声道分别进行 Delta 编码。

                Interleaved 输入：

                L0 R0 L1 R1 L2 R2

                对 L：
                L0
                L1 - L0
                L2 - L1

                对 R：
                R0
                R1 - R0
                R2 - R1
            */

            size_t frameCount =
                audioData.size() / channels_;

            std::vector<int16_t> previous(channels_, 0);

            for (size_t frame = 0; frame < frameCount; ++frame) {
                for (uint16_t channel = 0; channel < channels_; ++channel) {

                    size_t index =
                        frame * channels_ + channel;

                    int16_t current =
                        floatToInt16(audioData[index]);

                    int32_t delta;

                    if (frame == 0) {
                        delta = current;
                    }
                    else {
                        delta =
                            static_cast<int32_t>(current)
                            - static_cast<int32_t>(previous[channel]);
                    }

                    previous[channel] = current;

                    // Delta -> ZigZag
                    uint32_t encoded =
                        zigzagEncode(delta);

                    // ZigZag -> Varint
                    writeVarint(output, encoded);
                }
            }
        }

        // 解压
        void decompress(std::vector<float>& out, const std::vector<uint8_t>& data ) const {
            assert(data.size() >= 14);

            size_t position = 0;

            assert(data[position] == 'A' && data[position+1] == 'C' && data[position+2] == '0' && data[position+3] == '1');
            position += 4;

            // 读取 Header
            uint32_t sampleRate =
                readUint32(data, position);

            uint16_t channels =
                readUint16(data, position);

            uint32_t sampleCount =
                readUint32(data, position);

            assert(sampleRate == sampleRate_);
            assert(channels == channels_);
            assert(sampleCount % channels_ == 0);

            out.resize(sampleCount);

            size_t frameCount =
                sampleCount / channels_;

            std::vector<int16_t> previous(channels_, 0);

            for (size_t frame = 0; frame < frameCount; ++frame) {
                for (uint16_t channel = 0;
                    channel < channels_;
                    ++channel) {

                    // Varint
                    uint32_t encoded =
                        readVarint(data, position);

                    // ZigZag
                    int32_t delta =
                        zigzagDecode(encoded);

                    int32_t current;

                    if (frame == 0) {
                        current = delta;
                    }
                    else {
                        current =
                            static_cast<int32_t>(previous[channel])
                            + delta;
                    }

                    assert(current >= -32768 && current <= 32767);

                    int16_t sample =
                        static_cast<int16_t>(current);

                    previous[channel] = sample;

                    size_t index =
                        frame * channels_ + channel;

                    out[index] =
                        int16ToFloat(sample);
                }
            }

        }
    };

}