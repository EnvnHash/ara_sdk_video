//
// Created by user on 11.08.2021.
//

#include "VideoView.h"
#include "UIWindow.h"
#include "UIApplication.h"

using namespace glm;
using namespace std;
using namespace ara::av;

namespace ara {

VideoView::VideoView() : Image() {
    setName(getTypeName<VideoView>());
}

void VideoView::init() {
    Image::init();

    m_drawImmediate = true;
    m_color = glm::vec4{1.f};
    m_texShdr = getSharedRes()->shCol->getUIGridTexYuv();
}

void VideoView::setUrl(std::string&& url) {
    m_url = url;
}

void VideoView::setAssetName(std::string&& asset) {
    m_asset = asset;
}

bool VideoView::draw(uint32_t& objId) {
    Image::draw(objId);
    return drawFunc(objId);
}

bool VideoView::drawIndirect(uint32_t& objId) {
    Image::drawIndirect(objId);

    if (m_sharedRes && m_sharedRes->drawMan) {
        m_tempObjId = objId;
        m_sharedRes->drawMan->pushFunc([this] {
            m_dfObjId = m_tempObjId;
            drawFunc(m_dfObjId);
        });
    }

    return true;
}

bool VideoView::drawFunc(uint32_t& objId) {
#ifdef ARA_USE_FFMPEG
    m_imgFlags = 0;

    if (!m_running) {
        decoder = make_unique<FFMpegDecode>();

#ifdef __ANDROID__
        if (!m_asset.empty())
            decoder->OpenAndroidAsset(m_glbase, getApp()->m_androidApp, m_asset, 4, getWindow()->getWidthReal(), getWindow()->getHeightReal(), false, true);
        else
            decoder->OpenFile(m_glbase, m_url, 4, getWindow()->getWidthReal(), getWindow()->getHeightReal(), false, true);
#else
        decoder->openFile(m_glbase, m_url, 4, getWindow()->getWidthReal(), getWindow()->getHeightReal(), true, true);
#endif

        // decoder->openCamera(m_glbase, "/dev/video0", 720, 1280, false);
        decoder->start(0.0);

        m_running = true;
        m_timeStart = std::chrono::system_clock::now();
    }

    if (!m_yuvShdr) {
        m_yuvShdr = getSharedRes()->shCol->getUIGridTexYuv();
    }

    if (m_running && m_yuvShdr) {
        auto now = std::chrono::system_clock::now();
        auto actDifF = std::chrono::duration<double, std::milli>(now - m_timeStart).count();

        decoder->loadFrameToTexture(actDifF * 0.001);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (!m_texUniBlock.isInited()) {
            m_texUniBlock.init(m_yuvShdr->getProgram(), "nodeData");
            m_texUniBlock.update();
        }

        m_yuvShdr->begin();
        m_texUniBlock.bind();

        m_yuvShdr->setUniform1i("isYuv", (int)decoder->decodeYuv420OnGpu()); // y
        m_yuvShdr->setUniform1i("isNv12", (int)(decoder->getSrcPixFmt() == AV_PIX_FMT_NV12)); // y
        m_yuvShdr->setUniform1i("isNv21", (int)(decoder->getSrcPixFmt() == AV_PIX_FMT_NV21)); // y
#ifdef __ANDROID__
        m_yuvShdr->setUniform1i("rot90", 0); // y
#else
        m_yuvShdr->setUniform1i("rot90", 0); // y
#endif

        if (decoder->decodeYuv420OnGpu()) {
            m_yuvShdr->setUniform1f("alpha", 1.f); // y
            m_yuvShdr->setUniform1i("tex_unit", 0); // y
            m_yuvShdr->setUniform1i("u_tex_unit", 1); // u
            m_yuvShdr->setUniform1i("v_tex_unit", 2); // v

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, decoder->getTex()); // y

            if (decoder->getSrcPixFmt() == AV_PIX_FMT_YUV420P || decoder->getSrcPixFmt() == AV_PIX_FMT_NV12 || decoder->getSrcPixFmt() == AV_PIX_FMT_NV21) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, decoder->getTexU()); // u
            }

            if (decoder->getSrcPixFmt() == AV_PIX_FMT_YUV420P) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, decoder->getTexV()); // v
            }
        } else {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, decoder->getTex()); // y
        }

        glBindVertexArray(*m_sharedRes->nullVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_texUniBlock.unbind();

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
#endif

    getSharedRes()->requestRedraw = true;
    return true;
}

}