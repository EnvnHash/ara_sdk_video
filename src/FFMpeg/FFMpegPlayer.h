/*
 * FFMpegDecode.h
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#pragma once

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO) && defined(ARA_USE_GLBASE)

#include "FFMpeg/FFMpegDecode.h"
#include "Portaudio/Portaudio.h"

namespace ara::av {

class FFMpegPlayer : public FFMpegDecode {
public:
    void openFile(const ffmpeg::DecodePar& p) override;
    void start(double time) override;
    void stop() override;
    void recvAudioPacket(audioCbData& data);
    void shaderBegin();
    void loadFrameToTexture(double time);
    void clearResources() override;

    int32_t     getAudioWriteBufIdx() { return m_paudio.useCycleBuf() ? m_paudio.getCycleBuffer().getWritePos() : 0; }
    int32_t     getAudioReadBufIdx() { return m_paudio.useCycleBuf() ? m_paudio.getCycleBuffer().getReadPos() : 0; }
    Shaders*    getShader() { return m_shader; }

private:
    void allocateResources(ffmpeg::DecodePar& p) override;
    void allocGlRes(AVPixelFormat srcPixFmt);
    void initShader(AVPixelFormat srcPixFmt, ffmpeg::DecodePar& p);

    inline std::vector<std::unique_ptr<Texture>>& getTextures() { return m_textures; }

    inline GLuint	getTex() {  if (!m_textures.empty() && m_textures[0]->isAllocated()) return m_textures[0]->getId(); else return 0; }
    inline GLuint	getTexU() { if (m_textures.size() > 1 && m_textures[1]->isAllocated()) return m_textures[1]->getId(); else return 0; }
    inline GLuint	getTexV() { if (m_textures.size() > 2 && m_textures[2]->isAllocated()) return m_textures[2]->getId(); else return 0; }

    Portaudio m_paudio;
    uint32_t  m_cycBufSize = 128; // queue size in nr of PortAudio Frames, must be more or less equal to video queue in length
    size_t    m_bufSizeFact{};


    ShaderCollector*		            m_shCol=nullptr;
    Shaders*				            m_shader=nullptr;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<GLuint>			        m_pbos;

    bool						        m_usePbos=false; // review memory leaks if using m_pbos....
    bool						        m_glResInited=false;
    unsigned int 				        m_pboIndex = 0;
    double						        m_lastToGlTime = 0.0;
};

}

#endif