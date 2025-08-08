//
// Created by sven on 07-08-25.
//

#ifdef __ANDROID__

#pragma once

#include "FFMpeg/FFMpegDecode.h"

namespace ara::av {

class FFMpegDecodeMediaCodec : public FFMpegDecode {
public:
    void        openCamera(const ffmpeg::DecodePar& p) override;

protected:
    void        setDefaultHwDevice() override;
    bool        openAndroidAsset(const FFMpegDecodePar&);
    bool        openAsset(AAsset* assetDescriptor);
    bool        initMediaCode(AAsset* assetDescriptor);
    int         mediaCodecGetInputBuffer(AVPacket* packet);
    int         mediaCodecDequeueOutputBuffer();
    uint8_t*    mediaCodecGetOutputBuffer(int status, size_t& size);
    void        mediaCodecReleaseOutputBuffer(int status);
    void        parseVideoCodecPar(int32_t i, AVCodecParameters* localCodecParameters) override;
    int32_t     sendPacket(AVPacket* packet, AVCodecContext* codecContext) override;
    int32_t     checkReceiveFrame(AVCodecContext* codecContext) override;
    void        transferFromHwToCpu() override;
    void        clearResources();

    std::vector<std::vector<uint8_t>>	m_rawBuffer;
    AMediaExtractor*                    m_mediaExtractor = nullptr;
    AMediaCodec*                        m_mediaCodec = nullptr;
    AVBitStreamFilter*                  m_bsf = nullptr;
    AVBSFContext*                       m_bsfCtx=nullptr;
    AMediaCodecBufferInfo               m_mediaCodecInfo;
};

}

#endif