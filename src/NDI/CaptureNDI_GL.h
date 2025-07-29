#pragma once
#ifndef __ANDROID__

#include <utility>

#include "CaptureNDI.h"

#include "GLResNDI.h"

namespace ara::av::NDI {

class GLBufferClient : public BufferClient {
public:
    GLBufferClient(std::string& uname, std::string ipaddr) : BufferClient(uname, std::move(ipaddr)) {}
    GLBufferClient(Source& src) : BufferClient(src) {}

    void		InitGLRes(uint32_t scrWidth, uint32_t scrHeight, std::filesystem::path dataPath, GLBase* glbase);
    GLResNDI*   GetGlRes(){ return m_glRes.get(); }
    void		Upload();
    void		BindFrameTex(int texUnit=0, bool upload=true);

private:
    std::unique_ptr<GLResNDI> m_glRes;
};

}

#endif