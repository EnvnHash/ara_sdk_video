#ifndef __ANDROID__

#include "CaptureNDI_GL.h"

using namespace ara::av::NDI;
using namespace ara;
using namespace std;
namespace fs = std::filesystem;

void GLBufferClient::InitGLRes(uint32_t scrWidth, uint32_t scrHeight, fs::path dataPath, GLBase* glbase) {
	// this has to be called with an active OpenGL context
	m_glRes = make_unique<GLResNDI>();
	m_glRes->init(scrWidth, scrHeight, dataPath, glbase);
}

void GLBufferClient::Upload() {
	CIBuff* buf = GetLastBuff();

	if (buf && m_lastUplBuf != buf) {
		// if the width and height of the frameTex doesn't correspond to client, change it
		if (m_glRes->m_frameTex->getWidth() != buf->pixSize[0]
			|| m_glRes->m_frameTex->getHeight() != buf->pixSize[1]
			|| !m_glRes->m_frameTex->isAllocated()) {

			// if the texture did exist, free it								 
			if (m_glRes->m_frameTex->isAllocated()) {
                m_glRes->m_frameTex->releaseTexture();
            }

			// allocate
			m_glRes->m_frameTex->allocate2D(buf->pixSize[0], buf->pixSize[1], GL_RG8, GL_RG, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
		}

		// texture now for sure is there and in the right size, now upload!
		m_glRes->m_frameTex->upload((void*)&buf->img[0]);
		m_lastUplBuf = buf;
	}
}

void GLBufferClient::BindFrameTex(int texUnit, bool upload) {
	if (upload) {
        Upload();
    }
	m_glRes->m_frameTex->bind(texUnit);
}

#endif