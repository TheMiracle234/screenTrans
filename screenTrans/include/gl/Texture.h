// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <vector>
#include <glad/glad.h>

namespace gl {

	enum GpuTextureFmt : int{
		RGBA32F = GL_RGBA32F,
		RGBA32I = GL_RGBA32I,
		RGBA32UI = GL_RGBA32UI,
		RGBA16 = GL_RGBA16,
		RGBA16F = GL_RGBA16F,
		RGBA16I = GL_RGBA16I,
		RGBA16UI = GL_RGBA16UI,
		RGBA8 = GL_RGBA8,
		RGBA8UI = GL_RGBA8UI,
		SRGB8_ALPHA8 = GL_SRGB8_ALPHA8,
		RGB10_A2 = GL_RGB10_A2,
		RGB10_A2UI = GL_RGB10_A2UI,
		R11F_G11F_B10F = GL_R11F_G11F_B10F,
		RG32F = GL_RG32F,
		RG32I = GL_RG32I,
		RG32UI = GL_RG32UI,
		RG16 = GL_RG16,
		RG16F = GL_RG16F,
		RG8 = GL_RG8,
		RG8I = GL_RG8I,
		RG8UI = GL_RG8UI,
		R32F = GL_R32F,
		R32I = GL_R32I,
		R32UI = GL_R32UI,
		R16F = GL_R16F,
		R16I = GL_R16I,
		R16UI = GL_R16UI,
		R8 = GL_R8,
		R8I = GL_R8I,
		R8UI = GL_R8UI,
		RGBA16_SNORM = GL_RGBA16_SNORM,
		RGBA8_SNORM = GL_RGBA8_SNORM,
		RGB32F = GL_RGB32F,
		RGB32I = GL_RGB32I,
		RGB32UI = GL_RGB32UI,
		RGB16_SNORM = GL_RGB16_SNORM,
		RGB16F = GL_RGB16F,
		RGB16I = GL_RGB16I,
		RGB16UI = GL_RGB16UI,
		RGB16 = GL_RGB16,
		RGB8_SNORM = GL_RGB8_SNORM,
		RGB8 = GL_RGB8,
		RGB8I = GL_RGB8I,
		RGB8UI = GL_RGB8UI,
		SRGB8 = GL_SRGB8,
		RGB9_E5 = GL_RGB9_E5,
		RG16_SNORM = GL_RG16_SNORM,
		RG8_SNORM = GL_RG8_SNORM,
		COMPRESSED_RG_RGTC2 = GL_COMPRESSED_RG_RGTC2,
		COMPRESSED_SIGNED_RG_RGTC2 = GL_COMPRESSED_SIGNED_RG_RGTC2,
		R16_SNORM = GL_R16_SNORM,
		R8_SNORM = GL_R8_SNORM,
		COMPRESSED_RED_RGTC1 = GL_COMPRESSED_RED_RGTC1,
		COMPRESSED_SIGNED_RED_RGTC1 = GL_COMPRESSED_SIGNED_RED_RGTC1,
		DEPTH_COMPONENT32F = GL_DEPTH_COMPONENT32F,
		DEPTH_COMPONENT24 = GL_DEPTH_COMPONENT24,
		DEPTH_COMPONENT16 = GL_DEPTH_COMPONENT16,
		DEPTH32F_STENCIL8 = GL_DEPTH32F_STENCIL8,
		DEPTH24_STENCIL8 = GL_DEPTH24_STENCIL8,
	};

	enum class TextureFmt : unsigned int {
		RED = GL_RED,
		RG = GL_RG,
		RGB = GL_RGB,
		BGR = GL_BGR,
		RGBA = GL_RGBA,
		BGRA = GL_BGRA,
	};

	int currentTextureUnit();

	class Texture2D {
	private:
		GpuTextureFmt m_gpuFmt;
		TextureFmt m_fmt;
		unsigned int m_texture = 0;
		int m_imgW;
		int m_imgH;
		std::vector<uint8_t> m_data;
	public:
		Texture2D(int imgw, int imgh, std::vector<uint8_t> data, GpuTextureFmt gpu_fmt, TextureFmt fmt);
		Texture2D(const Texture2D&) = delete;
		Texture2D(Texture2D&& other) noexcept;
		Texture2D& operator=(const Texture2D&) = delete;
		void operator=(Texture2D&& other) noexcept;
		~Texture2D();

		std::vector<uint8_t>& data() { return m_data; }
		void resetData(std::vector<uint8_t> data, int c);
		void resize(int w, int h, int c);
		void unbind() const;
		void bind() const;
		void bindTo(int unit) const;

	};

	int currentTexture();
}