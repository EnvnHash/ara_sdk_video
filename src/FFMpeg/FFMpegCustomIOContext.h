//
// Created by sven on 20-08-25.
//

#pragma once

#include "FFMpeg/FFMpegCommon.h"

namespace ara::av::ffmpeg {

class CustomIOContext {
public:
    CustomIOContext() = default;
    explicit CustomIOContext(const std::string &datafile);
    ~CustomIOContext();

    void load(const std::string &datafile);
    void initAVFormatContext(AVFormatContext *);
    static int32_t IOReadFunc(void *data, uint8_t *buf, int32_t buf_size);
    static int64_t IOSeekFunc(void *data, int64_t pos, int32_t whence);

protected:
    AVIOContext*                    m_ioCtx{};
    uint8_t*                        m_ioBuffer{}; // internal buffer for ffmpeg
    int32_t                         m_ioBufferSize = 4096;
    std::vector<uint8_t>::iterator  m_bufPtr;
    std::vector<uint8_t>            m_buf;
};

}