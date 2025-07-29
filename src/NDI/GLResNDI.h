#ifndef __ANDROID__

#pragma once

#include <Utils/FBO.h>
#include <Shaders/ShaderCollector.h>
#include <Utils/Typo/TypoGlyphMap.h>

namespace ara::av::NDI {

class GLResNDI {
public:
    GLResNDI();
    ~GLResNDI() = default;

    void init(uint32_t scrWidth, uint32_t scrHeight, std::filesystem::path dataPath, GLBase* glbase);

    Shaders* initRenderShdr();
    void drawDisplayName();
    void destroy();

    Shaders*                        m_renderShdr = nullptr;
    Shaders*                        m_stdCol = nullptr;
    ShaderCollector                 m_shCol;
    std::unique_ptr<Texture>		m_frameTex;
    std::unique_ptr<Quad>           m_flipQuad;
    std::unique_ptr<Quad>           m_stdQuad;

private:
    bool							m_inited = false;
    glm::vec4                       m_typoColor;
    std::string						m_dispName;
    std::unique_ptr<TypoGlyphMap>   m_typo;
    int                             m_typoFontSize;
};

}

#endif