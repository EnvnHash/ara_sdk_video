//
// Created by sven on 20-08-25.
//

#include "FFMpeg/FFMpegCustomIOContext.h"
#include "AssetLoader.h"

namespace ara::av::ffmpeg {

CustomIOContext::CustomIOContext(const std::string &s) {
    load(s);
}

void CustomIOContext::load(const std::string &s) {
    m_ioBuffer = reinterpret_cast<uint8_t*>(av_malloc(m_ioBufferSize + AVPROBE_PADDING_SIZE)); // see destructor for details

    m_buf.clear();
    AssetLoader::loadAssetToMem(m_buf, s);
    m_bufPtr = m_buf.begin();

    m_ioCtx = avio_alloc_context(
        m_ioBuffer,
        m_ioBufferSize,                 // internal buffer and its size
        0,                              // write flag (1=true, 0=false)
        reinterpret_cast<void*>(this),  // user data, will be passed to our callback functions
        CustomIOContext::IOReadFunc,
        nullptr,                        // no writing
        CustomIOContext::IOSeekFunc
    );
}

void CustomIOContext::initAVFormatContext(AVFormatContext *pCtx) {
    pCtx->pb = m_ioCtx;
    pCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // or read some of the file and let ffmpeg do the guessing
    std::copy(m_bufPtr, m_bufPtr + m_ioBufferSize, m_ioBuffer);

    AVProbeData probeData;
    probeData.buf = m_ioBuffer;
    probeData.buf_size = m_ioBufferSize;
    probeData.filename = "";
    pCtx->iformat = av_probe_input_format(&probeData, 1);
}

int CustomIOContext::IOReadFunc(void *data, uint8_t *buf, int buf_size) {
    auto hctx = reinterpret_cast<CustomIOContext*>(data);

    auto numToRead = std::min(hctx->m_ioBufferSize, static_cast<int32_t>(std::distance(hctx->m_bufPtr, hctx->m_buf.end())));
    std::copy(hctx->m_bufPtr, hctx->m_bufPtr + numToRead, hctx->m_ioBuffer);
    std::advance(hctx->m_bufPtr, numToRead);

    return numToRead;
}

// whence: SEEK_SET, SEEK_CUR, SEEK_END (like fseek) and AVSEEK_SIZE
int64_t CustomIOContext::IOSeekFunc(void *data, int64_t pos, int whence) {
    auto hctx = reinterpret_cast<CustomIOContext*>(data);
    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(hctx->m_buf.size());
    }

    hctx->m_bufPtr = hctx->m_buf.begin();
    std::advance(hctx->m_bufPtr, pos);
    return pos;
}

CustomIOContext::~CustomIOContext() {
    av_free(m_ioBuffer);
    av_free(m_ioCtx);
}

}