//
// Created by sven on 07-08-25.
//

#ifdef __ANDROID__

#pragma once

#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include "FFMpeg/FFMpegDecode.h"

namespace ara::av {

struct memin_buffer_data {
    uint8_t *ptr=nullptr;
    size_t size=0; ///< size left in the buffer
    uint8_t *start=nullptr;
    size_t fileSize=0;
};

class FFMpegDecodeMediaCodec : public FFMpegDecode {
public:
    void        openCamera(const ffmpeg::DecodePar& p) override;

protected:
    void        setDefaultHwDevice() override;
    bool        openAndroidAsset(const ffmpeg::DecodePar&);
    bool        openAsset(AAsset* assetDescriptor);
    bool        initMediaCode(AAsset* assetDescriptor);
    int         mediaCodecGetInputBuffer(AVPacket* packet);
    int         mediaCodecDequeueOutputBuffer();
    uint8_t*    mediaCodecGetOutputBuffer(int status, size_t& size);
    void        mediaCodecReleaseOutputBuffer(int status);
    void        parseVideoCodecPar(int32_t i, AVCodecParameters* localCodecParameters,  const AVCodec*) override;
    static int  readPacketFromInbuf(void *opaque, uint8_t *buf, int buf_size);
    int32_t     sendPacket(AVPacket* packet, AVCodecContext* codecContext) override;
    int32_t     checkReceiveFrame(AVCodecContext* codecContext) override;
    void        transferFromHwToCpu() override;
    void        clearResources() override;

    std::vector<std::vector<uint8_t>>	m_rawBuffer;
    AMediaExtractor*                    m_mediaExtractor = nullptr;
    AMediaCodec*                        m_mediaCodec = nullptr;
    AVBitStreamFilter*                  m_bsf = nullptr;
    AVBSFContext*                       m_bsfCtx=nullptr;
    AMediaCodecBufferInfo               m_mediaCodecInfo;
};

}

#endif