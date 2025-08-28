/*
 * FFMpegDecode.cpp
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO) && defined(ARA_USE_GLBASE)

#include <GLBase.h>
#include "FFMpegPlayer.h"
#include "Portaudio/PortaudioAudioEngine.h"

using namespace glm;
using namespace std;
using namespace ara::av::ffmpeg;

namespace ara::av {
void FFMpegPlayer::openFile(const ffmpeg::DecodePar& p) {
    if (p.glbase){
        m_shCol = &p.glbase->shaderCollector();
    }
    FFMpegAudioPlayer::openFile(p);
}

void FFMpegPlayer::openCamera(const ffmpeg::DecodePar& p) {
    if (p.glbase){
        m_shCol = &p.glbase->shaderCollector();
    }
    FFMpegDecode::openCamera(p);
}

void FFMpegPlayer::allocateResources(ffmpeg::DecodePar& p) {
    FFMpegAudioPlayer::allocateResources(p);

    if (m_videoCodecCtx) {
        if (!p.decodeYuv420OnGpu && p.destWidth && p.destHeight) {
            m_bgraFrame.allocate(m_videoFrameBufferSize);
            for (auto& it : m_bgraFrame.getBuffer()) {
                it.frame = allocPicture(m_destPixFmt, p.destWidth, p.destHeight, &it.buf);
            }
        }

        // destFmt BGRA
        if (m_usePbos) {
            m_pbos = vector<GLuint>(m_nrPboBufs);
            std::fill(m_pbos.begin(), m_pbos.end(), 0);
        }
    }
}

void FFMpegPlayer::allocGlRes(AVPixelFormat srcPixFmt) {
    initShader(srcPixFmt, m_par);
    m_nrTexBuffers = !m_par.decodeYuv420OnGpu ? 1 : (srcPixFmt == AV_PIX_FMT_NV12 || srcPixFmt == AV_PIX_FMT_NV21) ? 2 : 3;
    m_textures = vector<Texture>(m_nrTexBuffers);

    for (auto &it : m_textures) {
        it.setGlbase(m_par.glbase);
    }

    if (m_par.decodeYuv420OnGpu) {
        if (m_srcPixFmt == AV_PIX_FMT_NV12 || m_srcPixFmt == AV_PIX_FMT_NV21) {
            m_textures[0].allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1].allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_RG8, GL_RG, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        } else {   // YUV420P
            m_textures[0].allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1].allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[2].allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        }
    } else {
        m_textures[0].allocate2D(m_par.destWidth, m_par.destHeight, GL_RGB8, GL_RGB, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
    }
}

void FFMpegPlayer::initShader(AVPixelFormat srcPixFmt, ffmpeg::DecodePar& p) {
    if (p.decodeYuv420OnGpu) {
        std::string vert = ShaderCollector::getShaderHeader() + "// yuv420 texture shader, vert\n" + getVertShader();
        std::string frag = getFragShaderHeader();

        if (srcPixFmt == AV_PIX_FMT_NV12) {
            frag += getNv12FragShader();
        } else if (srcPixFmt == AV_PIX_FMT_NV21) {
            frag += getNv21FragShader();
        } else {
            frag += getYuv420FragShader();
        }

        frag += "}";
        frag = ShaderCollector::getShaderHeader() + "// YUV420 fragment shader\n"  + frag;

        m_shader = m_shCol->add("FFMpegDecode_yuv", vert, frag);
    } else {
        m_shader = m_shCol->getStdTexAlpha();
    }
}

std::string FFMpegPlayer::getVertShader() {
    return STRINGIFY(
        layout(location = 0) in vec4 position;    \n
        layout(location = 2) in vec2 texCoord;    \n
        uniform mat4 m_pvm;                       \n
        out vec2 tex_coord;                       \n
        void main() {                             \n
        \t tex_coord = texCoord;                  \n
        \t gl_Position = m_pvm * position;        \n
    });
}

std::string FFMpegPlayer::getFragShaderHeader() {
    return STRINGIFY(
        uniform sampler2D tex_unit;                 \n // Y component
        uniform sampler2D u_tex_unit;               \n // U component
        uniform sampler2D v_tex_unit;               \n // V component
        uniform float alpha;                        \n // V component
                                                    \n
        in vec2 tex_coord;                          \n
        layout(location = 0) out vec4 fragColor;    \n
        void main() {                               \n);
}

std::string FFMpegPlayer::getNv12FragShader() {
    return STRINGIFY(
        float y = texture(tex_unit, tex_coord).r;              \n
        float u = texture(u_tex_unit, tex_coord).r - 0.5;      \n
        float v = texture(u_tex_unit, tex_coord).g - 0.5;      \n

        fragColor = vec4((vec3(y + 1.4021 * v,                 \n
                               y - 0.34482 * u - 0.71405 * v,  \n
                               y + 1.7713 * u)                 \n
                          - 0.05) * 1.07,                      \n
                         alpha);                               \n
    );
}

std::string FFMpegPlayer::getNv21FragShader() {
    return STRINGIFY(
        float y = texture(tex_unit, tex_coord).r;             \n
        float u = texture(u_tex_unit, tex_coord).g - 0.5;     \n
        float v = texture(u_tex_unit, tex_coord).r - 0.5;     \n
        fragColor = vec4((vec3(y + 1.4021 * v,                \n
                               y - 0.34482 * u - 0.71405 * v, \n
                               y + 1.7713 * u)                \n
                               - 0.05) * 1.07,                \n
                          alpha);                             \n
    );
}

std::string FFMpegPlayer::getYuv420FragShader() {
    // YUV420 is a planar (non-packed) m_format.
    // the first plane is the Y with one byte per pixel.
    // the second plane us U with one byte for each 2x2 square of pixels
    // the third plane is V with one byte for each 2x2 square of pixels
    //
    // tex_unit - contains the Y (luminance) component of the
    //    image. this is a texture unit set up by the OpenGL program.
    // u_texture_unit, v_texture_unit - contain the chrominance parts of
    //    the image. also texture units  set up by the OpenGL program.

    return STRINGIFY(
        float y = texture(tex_unit, tex_coord).r;           \n
        float u = texture(u_tex_unit, tex_coord).r - 0.5;   \n
        float v = texture(v_tex_unit, tex_coord).r - 0.5;   \n

        float r = y + 1.402 * v;                            \n
        float g = y - 0.344 * u - 0.714 * v;                \n
        float b = y + 1.772 * u;                            \n

        fragColor = vec4(vec3(r, g, b), alpha);             \n
    );
}

void FFMpegPlayer::shaderBegin() {
    if (m_run && m_shader && !m_textures.empty()) {
        m_shader->begin();
        m_shader->setIdentMatrix4fv("m_pvm");
        m_shader->setUniform1f("alpha", 1.f); // y

        if (m_par.decodeYuv420OnGpu) {
            m_shader->setUniform1i("tex_unit", 0); // y
            m_shader->setUniform1i("u_tex_unit", 1); // u
            m_shader->setUniform1i("v_tex_unit", 2); // v

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[0].getId()); // y

            if (m_srcPixFmt == AV_PIX_FMT_YUV420P || m_srcPixFmt == AV_PIX_FMT_NV12) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_textures[1].getId()); // u
            }

            if (m_srcPixFmt == AV_PIX_FMT_YUV420P) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, m_textures[2].getId()); // v
            }
        } else {
            m_shader->setUniform1i("tex", 0); // y
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[0].getId()); // y
        }
    }
}

int64_t FFMpegPlayer::loadFrameToTexture(double time, bool monotonic) {
    int64_t framePts{};

    if (m_resourcesAllocated && m_run && !m_pause && m_frames.getFillAmt() > 0) {
        m_actRelTime = getActRelTime(time);

        if (!m_glResInited && m_srcWidth && m_srcHeight) {
            allocGlRes(m_srcPixFmt);
            m_glResInited = true;
        }

        if (calcFrameToUpload(m_actRelTime, time, monotonic)
            && m_consumeFrames
            && m_frames.getReadBuff().frame->width
            && m_frames.getReadBuff().frame->height) {

            if (m_par.decodeYuv420OnGpu) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                if ((AV_PIX_FMT_NV12 == m_srcPixFmt || AV_PIX_FMT_NV21 == m_srcPixFmt) ) {
                    uploadNvFormat();
                } else if ((AV_PIX_FMT_YUV420P == m_srcPixFmt || AV_PIX_FMT_YUYV422 == m_srcPixFmt) && !m_textures.empty()) {
                    uploadYuv420();
                }
            } else {
                if (m_usePbos) {
                    uploadViaPbo();
                } else {
                    uploadRgba();
                }
                if (m_downFrameCb) {
                    m_downFrameCb(m_bgraFrame.getReadBuff().frame->data[0]);
                }
            }

            // mark as consumed
            framePts = m_frames.getReadBuff().frame->best_effort_timestamp;
            m_lastReadPtss = m_frames.getReadBuff().ptss;
            m_frames.getReadBuff().frame->pts = -1;
            m_frames.consumeCountUp();
            m_decodeCond.notify();     // wait until the packet was needed
            m_lastToGlTime = time;
        }
    }

    return framePts;
}

bool FFMpegPlayer::calcFrameToUpload(double& actRelTime, double time, bool monotonic) {
    if (monotonic) {
        m_consumeFrames = true;
        m_firstFramePresented = true;
        return true;
    } else if (!m_hasNoTimeStamp) {
        // time stabilization, try to take the next frame in queue, but advance if there is a frame closer to the actual time
        if (!m_firstFramePresented) {
            m_firstFramePresented = true;
            m_startTime = time;
            m_consumeFrames = true;
            return true;
        } else {
            if (m_lastReadPtss > m_frames.getReadBuff().ptss) {
                m_startTime = time;
                actRelTime = getActRelTime(time);
            }

            auto readPosTimeDiff = fmod(actRelTime, getDurationSec(streamType::video)) - m_frames.getReadBuff().ptss;
            if (readPosTimeDiff < 0) {
                return false;
            } else {
                for (auto i=1; i<m_frames.getFillAmt()-1; ++i) {
                    auto idx = (m_frames.getReadPos() + i) % m_frames.getCapacity();
                    auto diff = actRelTime - m_frames.getBuffer()[idx].ptss;
                    if (diff > 0 && diff < fabs(readPosTimeDiff)) {
                        m_frames.setReadPos(idx);
                        readPosTimeDiff = actRelTime - m_frames.getReadBuff().ptss;
                    }
                }
            }
            return true;
        }
    } else {
        // in case there is no timestamp just take the framerate to offset
        if (!m_consumeFrames && m_frames.getFillAmt() >= m_nrFramesToStart) {
            m_consumeFrames = true;
            m_startTime = time;
            actRelTime = 0.0;
        }

        auto uploadNewFrame = m_consumeFrames && m_frames.getFillAmt() > 1 && (time - m_lastToGlTime >= (0.8f / m_fps[toType(streamType::video)]));
        if (uploadNewFrame) {
            actRelTime = time - m_startTime;
            ++m_frameToUpload %= m_videoFrameBufferSize;
        }
        return uploadNewFrame;
    }
}

void FFMpegPlayer::uploadNvFormat() {
    // UV interleaved
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textures[1].getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth / 2, m_srcHeight / 2,
                    GL_RG, GL_UNSIGNED_BYTE, m_frames.getReadBuff().frame->data[1]);

    // Y
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[0].getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth, m_srcHeight,
                    ffmpeg::getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                    m_frames.getReadBuff().frame->data[0]);
}

void FFMpegPlayer::uploadYuv420() {
    // if the video is encoded as YUV420 it's in three separate areas in
    // memory (planar-- not interlaced). so if we have three areas of
    // data, set up one texture unit for each one. in the if statement
    // we'll set up the texture units for chrominance (U & V) and we'll
    // put the luminance (Y) data in GL_TEXTURE0 after the if.

    // luminance values, whole picture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[0].getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth, m_srcHeight,
                    ffmpeg::getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                    m_frames.getReadBuff().frame->data[0]);

    int chroma_width = m_srcWidth / 2;
    int chroma_height = m_srcHeight / 2;

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textures[1].getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                    ffmpeg::getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                    m_frames.getReadBuff().frame->data[1]);


    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textures[2].getId());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                    ffmpeg::getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                    m_frames.getReadBuff().frame->data[2]);
}

void FFMpegPlayer::uploadViaPbo() {
    m_pboIndex = (m_pboIndex + 1) % m_nrPboBufs;
    uint nextIndex = (m_pboIndex + 1) % m_nrPboBufs;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textures[0].getId());
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[m_pboIndex]);

    glTexSubImage2D(GL_TEXTURE_2D,      // target
                    0,                      // First mipmap level
                    0, 0,                   // x and y offset
                    m_par.destWidth,              // width and height
                    m_par.destHeight,
                    GL_BGR,
                    GL_UNSIGNED_BYTE,
                    nullptr);


    // bind PBO to update texture source
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[nextIndex]);

    // Note that glMapBufferARB() causes sync issue.
    // If GPU is working with this buffer, glMapBufferARB() will wait(stall)
    // until GPU to finish its job. To avoid waiting (idle), you can call
    // first glBufferDataARB() with nullptr pointer before glMapBufferARB().
    // If you do that, the previous data in PBO will be discarded and
    // glMapBufferARB() returns a new allocated pointer immediately
    // even if GPU is still working with the previous data.
    glBufferData(GL_PIXEL_UNPACK_BUFFER, m_par.destWidth * m_par.destHeight * 4, nullptr, GL_STREAM_DRAW);

    // map the buffer object into client's memory
    auto ptr = reinterpret_cast<GLubyte *>(glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, m_par.destWidth * m_par.destHeight * 4, GL_MAP_WRITE_BIT));

    if (ptr && !m_frames.empty())
    {
        // update data directly on the mapped buffer
        memcpy(ptr, m_bgraFrame.getReadBuff().frame->data[0], m_par.destWidth * m_par.destHeight * 4);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release the mapped buffer
    }

    // it is good idea to release PBOs with ID 0 after use.
    // Once bound with 0, all pixel operations are back to normal ways.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void FFMpegPlayer::uploadRgba() {
    if (!m_frames.empty()) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0].getId());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_par.destWidth, m_par.destHeight,
                        GL_BGR, GL_UNSIGNED_BYTE, m_bgraFrame.getReadBuff().frame->data[0]);
    }
}

void FFMpegPlayer::clearResources() {
    FFMpegAudioPlayer::clearResources();
    if (m_usePbos){
        glDeleteBuffers(static_cast<GLsizei>(m_nrPboBufs), &m_pbos[0]);
        m_pbos.clear();
    }
}

}

#endif