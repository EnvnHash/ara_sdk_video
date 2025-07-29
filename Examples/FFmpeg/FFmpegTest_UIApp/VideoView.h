//
// Created by user on 11.08.2021.
//

#pragma once

#include <FFMpeg/FFMpegDecode.h>

#include <UIElements/Image.h>
#include <UIElements/Label.h>
#include <UIElements/Spinner.h>
#include <Utils/Texture.h>

namespace ara {

class VideoView : public Image {
public:
    VideoView();
    virtual ~VideoView()=default;

    void init();
    bool draw(uint32_t& objId);
    bool drawIndirect(uint32_t& objId);
    bool drawFunc(uint32_t& objId);

    void setUrl(std::string&& stream);
    void setAssetName(std::string&& asset);

private:
    UINode*               m_buffCont=nullptr;
    Spinner*              m_bufferSpinner=nullptr;
    Label*                m_bufferingLbl=nullptr;
    Label*                m_streamLabel=nullptr;
    std::string           m_url;
    std::string           m_asset;
    uint32_t              m_tempObjId=0;
    uint32_t              m_dfObjId=0;
    //cap::FFMpegDecodeAudio      decoder;
    std::unique_ptr<ara::av::FFMpegDecode>  decoder;
    std::unique_ptr<Quad>       m_normQuad;
    Shaders*                    m_yuvShdr=nullptr;
    bool                        m_running = false;

    std::chrono::time_point<std::chrono::system_clock> m_timeStart;
};

}

