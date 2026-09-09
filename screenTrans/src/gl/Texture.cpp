// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <gl/Texture.h>
#include <gl/debug.h>
#include <cassert>

namespace gl {
	Texture2D::Texture2D(int imgw, int imgh, std::vector<uint8_t> data, GpuTextureFmt gpu_fmt, TextureFmt fmt):
		m_gpuFmt(gpu_fmt), m_fmt(fmt), m_data(std::move(data)),
		m_imgW(imgw),
		m_imgH(imgh)
	{
		GLCALL(glGenTextures(1, &m_texture));
		GLCALL(glBindTexture(GL_TEXTURE_2D, m_texture));

		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
		GLCALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));

		GLCALL(glTexImage2D(GL_TEXTURE_2D, 0, (GLint)gpu_fmt, m_imgW, m_imgH, 0, (GLenum)fmt, GL_UNSIGNED_BYTE, m_data.data()));
		GLCALL(glGenerateMipmap(GL_TEXTURE_2D));
	}

	Texture2D::Texture2D(Texture2D&& other) noexcept:
		m_gpuFmt(other.m_gpuFmt), 
		m_fmt(other.m_fmt),
		m_texture(other.m_texture),
		m_imgW(other.m_imgW),
		m_imgH(other.m_imgH),
		m_data(std::move(other.m_data))
	{
		other.m_texture = 0;
	}

	void Texture2D::operator=(Texture2D&& other) noexcept {
		if (&other == this)
			return;
		m_texture = (other.m_texture);
		m_imgW = (other.m_imgW);
		m_imgH = (other.m_imgH);
		m_data = (std::move(other.m_data));

		other.m_texture = 0;
	}

	Texture2D::~Texture2D() {
		GLCALL(glDeleteTextures(1, &m_texture));
	}

	int currentTextureUnit() {
		int unit;
		GLCALL(glGetIntegerv(GL_ACTIVE_TEXTURE, &unit));
		return unit - GL_TEXTURE0;
	}

	void Texture2D::bind() const {
		GLCALL(glBindTexture(GL_TEXTURE_2D, m_texture));
	}

	void Texture2D::bindTo(int unit) const {
		GLCALL(glActiveTexture(GL_TEXTURE0 + unit));
		GLCALL(glBindTexture(GL_TEXTURE_2D, m_texture));
	}

	void Texture2D::resetData(std::vector<uint8_t> data, int c) {
		assert(currentTexture() == m_texture);
		assert(data.size() == m_imgW * m_imgH * c);
		m_data = std::move(data);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_imgW, m_imgH, (GLenum)m_fmt, GL_UNSIGNED_BYTE, m_data.data());
	}

	void Texture2D::resize(int w, int h, int c) {
		assert(currentTexture() == m_texture);
		if (w == m_imgW && h == m_imgH) { return; }
		m_imgW = w;
		m_imgH = h;
		m_data.resize(m_imgW * m_imgH * c);
		GLCALL(glTexImage2D(GL_TEXTURE_2D, 0, (GLint)m_gpuFmt, m_imgW, m_imgH, 0, (GLenum)m_fmt, GL_UNSIGNED_BYTE, m_data.data()));
		GLCALL(glGenerateMipmap(GL_TEXTURE_2D));
	}

	void Texture2D::unbind() const {
		assert(currentTexture() == m_texture);
		GLCALL(glBindTexture(GL_TEXTURE_2D, 0));
	}

	int currentTexture() {
		int texture;
		GLCALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture));
		return texture;
	}

}