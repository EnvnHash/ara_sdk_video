/*
 * FFMpegDecode.h
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#pragma once

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO) && defined(ARA_USE_GLBASE)

#include "FFMpeg/FFMpegAudioPlayer.h"
#include "Portaudio/Portaudio.h"

namespace ara::av {

class FFMpegPlayer : public FFMpegAudioPlayer {
public:
    void openFile(const ffmpeg::DecodePar& p) override;
    void openCamera(const ffmpeg::DecodePar& p) override;
    void shaderBegin();
    int64_t loadFrameToTexture(double time, bool monotonic=false);

    Shaders*    getShader() { return m_shader; }
    GLuint      getTex() {  if (!m_textures.empty() && m_textures[0].isAllocated()) return m_textures[0].getId(); else return 0; }
    GLuint      getTexU() { if (m_textures.size() > 1 && m_textures[1].isAllocated()) return m_textures[1].getId(); else return 0; }
    GLuint      getTexV() { if (m_textures.size() > 2 && m_textures[2].isAllocated()) return m_textures[2].getId(); else return 0; }

    void setShaderCollector(ShaderCollector* shCol) { m_shCol = shCol; }
    void setVideoContext(AVCodecContext* ctx) { m_videoCodecCtx = ctx; }
    void allocateResources(const ffmpeg::DecodePar& p) override;

private:
    void allocGlRes(AVPixelFormat srcPixFmt);
    void initShader(AVPixelFormat srcPixFmt, ffmpeg::DecodePar& p);
    bool calcFrameToUpload(double& actRelTime, double time, bool monotonic);
    void uploadNvFormat();
    void uploadYuv420();
    void uploadViaPbo();
    void uploadRgba();
    void clearResources() override;

    double getActRelTime(double time) { return time - m_startTime + static_cast<double>(m_videoStartPts) * m_timeBaseDiv[toType(ffmpeg::streamType::video)]; }

    static std::string getVertShader();
    static std::string getFragShaderHeader();
    static std::string getNv12FragShader();
    static std::string getNv21FragShader();
    static std::string getYuv420FragShader();

    std::vector<Texture>& getTextures() { return m_textures; }

    size_t                  m_bufSizeFact{};
    ShaderCollector*		m_shCol=nullptr;
    Shaders*				m_shader=nullptr;
    std::vector<Texture>    m_textures;
    std::vector<GLuint>		m_pbos;
    bool					m_usePbos=false; // review memory leaks if using m_pbos....
    bool					m_glResInited=false;
    unsigned int 			m_pboIndex = 0;
    double					m_actRelTime = 0.0;
    double					m_lastToGlTime = 0.0;
    double					m_lastReadPtss = -1.0;
};

}

#endif