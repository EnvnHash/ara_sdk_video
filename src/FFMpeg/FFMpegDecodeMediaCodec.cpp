//
// Created by sven on 07-08-25.
//

#ifdef __ANDROID__

#include "FFMpeg/FFMpegDecodeMediaCodec.h"

using namespace std;

namespace ara::av {

void FFMpegDecodeMediaCodec::openCamera(const ffmpeg::DecodePar& p) {
    m_par = p;
    m_par.useNrThreads = 2;
    m_hasNoTimeStamp = true;
    m_isStream = true;
    m_videoFrameBufferSize = 2;

    ffmpeg::initFFMpeg();
    try {
        allocFormatContext();

        auto camInputFormat = av_find_input_format("android_camera");
        if (!camInputFormat) {
            throw runtime_error("ERROR couldn't find input m_format android_camera");
        }

        av_dict_set(&m_formatOpts, "video_size", "720x1280", 0);
        av_dict_set_int(&m_formatOpts, "camera_index",0, 0);
        av_dict_set_int(&m_formatOpts, "input_queue_size", 1, 0);

        setupStreams(camInputFormat, &m_formatOpts, m_par);
    } catch (std::runtime_error& e) {
        LOGE << "FFMpegDecodeMediaCodec::openFile Error: " << e.what();
    }
}

bool FFMpegDecodeMediaCodec::openAndroidAsset(const ffmpeg::DecodePar& p) {
    m_par = p;
    m_resourcesAllocated = false;
    m_videoFrameBufferSize = p.useHwAccel ? 32 : 32;

    avformat_network_init();
    m_logLevel = AV_LOG_INFO;
    av_log_set_level(m_logLevel);
    av_log_set_callback( &ffmpeg::LogCallbackShim );	// custom logging
    allocFormatContext();

    if (p.assetName.empty()) {
        return false;
    }

    AAsset* assetDescriptor = AAssetManager_open(p.app->activity->assetManager, p.assetName.c_str(), AASSET_MODE_BUFFER);
    if (p.useHwAccel) {
        initMediaCode(assetDescriptor);
    }
    openAsset(assetDescriptor);

    if (p.startDecodeThread) {
        m_decodeThread = std::thread([this] {
                                         allocateResources(m_par);
                                         m_startTime = 0.0;
                                         m_run = true;
                                         setupStreams(nullptr, &m_formatOpts, m_par);
                                         singleThreadDecodeLoop();
                                     });
        m_decodeThread.detach();
        return true;
    } else {
        return setupStreams(nullptr, &m_formatOpts, m_par);
    }
}

bool FFMpegDecodeMediaCodec::initMediaCode(AAsset* assetDescriptor) {
    off_t outStart, outLen;
    int fd = AAsset_openFileDescriptor(assetDescriptor, &outStart, &outLen);

    m_mediaExtractor = AMediaExtractor_new();
    auto err = AMediaExtractor_setDataSourceFd(m_mediaExtractor, fd, static_cast<off64_t>(outStart),static_cast<off64_t>(outLen));
    close(fd);
    if (err != AMEDIA_OK) {
        LOGE << "FFmpegDecode::OpenAndroidAsset AMediaExtractor_setDataSourceFd fail. err=" << err;
        return false;
    }

    int numTracks = AMediaExtractor_getTrackCount(m_mediaExtractor);

    LOG << "FFmpegDecode::OpenAndroidAsset AMediaExtractor_getTrackCount " << numTracks << " tracks";
    for (int i = 0; i < numTracks; ++i) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(m_mediaExtractor, i);
        auto s = AMediaFormat_toString(format);
        LOG << "FFmpegDecode::OpenAndroidAsset track " << i << " format:" << s;

        const char *mime;
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            LOGE << "FFmpegDecode::OpenAndroidAsset no mime type";
            AMediaFormat_delete(format);
            format = nullptr;
            return false;
        } else if (!strncmp(mime, "video/", 6)) {
            // Omitting most error handling for clarity.
            // Production code should check for errors.
            AMediaExtractor_selectTrack(m_mediaExtractor, i);
            m_mediaCodec = AMediaCodec_createDecoderByType(mime);
            if (!m_mediaCodec) {
                LOGE << "FFmpegDecode::OpenAndroidAsset create media codec fail.";
                return false;
            }
            AMediaCodec_configure(m_mediaCodec, format, nullptr, nullptr, 0);
            AMediaCodec_start(m_mediaCodec);
            AMediaFormat_delete(format);
            format = nullptr;
            return true;
        }

        if (format) {
            AMediaFormat_delete(format);
        }
    }

    return true;
}

bool FFMpegDecodeMediaCodec::openAsset(AAsset* assetDescriptor) {
    size_t fileLength = AAsset_getLength(assetDescriptor);

    m_memInputBuf.resize(fileLength);
    int64_t readSize = AAsset_read(assetDescriptor, m_memInputBuf.data(), m_memInputBuf.size());

    AAsset_close(assetDescriptor);

    // Alloc a buffer for the stream
    auto fileStreamBuffer = (unsigned char*)av_malloc(m_avioCtxBufferSize);
    if (!fileStreamBuffer){
        LOGE << "OpenAndroidAsset failed out of memory";
        return false;
    }

    m_meminBuffer.size = m_memInputBuf.size();
    m_meminBuffer.ptr = &m_memInputBuf[0];
    m_meminBuffer.start = &m_memInputBuf[0];
    m_meminBuffer.fileSize = fileLength;

    // Get a AVContext stream
    m_ioContext = avio_alloc_context(
            fileStreamBuffer,           // Buffer
            m_avioCtxBufferSize,     // Buffer size
            0,                          // Buffer is only readable - set to 1 for read/write
            &m_meminBuffer,            // User (your) specified data
            &FFMpegDecodeMediaCodec::readPacketFromInbuf,      // Function - Reading Packets (see example)
            0,                          // Function - Write Packets
            nullptr                     // Function - Seek to position in stream (see example)
    );

    if (!m_ioContext) {
        LOGE << "OpenAndroidAsset failed out of memory";
        return false;
    }

    // Set up the Format Context
    m_formatContext->pb = m_ioContext;
    m_formatContext->flags |= AVFMT_FLAG_CUSTOM_IO; // we set up our own IO

   return true;
}

int FFMpegDecodeMediaCodec::readPacketFromInbuf(void *opaque, uint8_t *buf, int buf_size) {
    auto *bd = (struct memin_buffer_data*) opaque;
    buf_size = std::min<int>(buf_size, (int)bd->size);

    // loop
    if (!buf_size)
        bd->ptr = bd->start;

    // copy internal buffer data to buf
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;

    return buf_size;
}

void FFMpegDecodeMediaCodec::setDefaultHwDevice() {
    m_defaultHwDevType = "mediacodec";
}

void FFMpegDecodeMediaCodec::parseVideoCodecPar(int32_t i, AVCodecParameters* localCodecParameters, const AVCodec*) {
    m_bsf = (AVBitStreamFilter*) av_bsf_get_by_name((char*)"h264_mp4toannexb");
        if(!m_bsf){
            LOGE << "bitstreamfilter not found";
            return;
        }
        int ret=0;
        if ((ret = av_bsf_alloc(m_bsf, &m_bsfCtx)))
            return;
        if (((ret = avcodec_parameters_from_context(m_bsfCtx->par_in, m_videoCodecCtx)) < 0) ||
            ((ret = av_bsf_init(m_bsfCtx)) < 0)) {
            av_bsf_free(&m_bsfCtx);
            LOGE << "av_bsf_init failed";
            return;
        }
}

int32_t FFMpegDecodeMediaCodec::sendPacket(AVPacket* packet, AVCodecContext*) {
    return mediaCodecGetInputBuffer(packet);
}

int32_t FFMpegDecodeMediaCodec::checkReceiveFrame(AVCodecContext* codecContext) {
    return mediaCodecDequeueOutputBuffer();
}

void FFMpegDecodeMediaCodec::transferFromHwToCpu() {
    size_t hwBufSize;
    int response=0;
    auto buffer = mediaCodecGetOutputBuffer(response, hwBufSize);

    memcpy(&m_frames.getWriteBuff().frame->data[0][0], buffer, hwBufSize);

    m_frames.getWriteBuff().frame->pts = m_mediaCodecInfo.presentationTimeUs * av_q2d(m_formatContext->streams[toType(ffmpeg::streamType::video)]->time_base) * 1000;
    m_frames.getWriteBuff().frame->pict_type = m_frame->pict_type;

    mediaCodecReleaseOutputBuffer(response);
}

int FFMpegDecodeMediaCodec::mediaCodecGetInputBuffer(AVPacket* packet) {
    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(m_mediaCodec, 0);

    // in case we got a valid input buffer index copy the packet into it
    if (bufIdx >= 0)
    {
        size_t bufSize;
        auto buf = AMediaCodec_getInputBuffer(m_mediaCodec, bufIdx, &bufSize);

        if (m_bsfCtx) {
            if (av_bsf_send_packet(m_bsfCtx, packet) < 0) {
                LOGE << "FFMpegDecode::mediaCodecGetInputBuffer: av_bsf_send_packet failed!";
                return -1;
            }
            if (av_bsf_receive_packet(m_bsfCtx, packet) < 0) {
                LOGE << "FFMpegDecode::mediaCodecGetInputBuffer: av_bsf_receive_packet failed!";
                return -1;
            }
        }

        memcpy(buf, packet->data, packet->size);
        AMediaCodec_queueInputBuffer(m_mediaCodec, bufIdx, 0, packet->size, packet->pts, 0);
    }

    return 0;
}

int FFMpegDecodeMediaCodec::mediaCodecDequeueOutputBuffer() {
    // try to get a valid output buffer
    auto status = AMediaCodec_dequeueOutputBuffer(m_mediaCodec, &m_mediaCodecInfo, 1000);

    if (status >= 0) {
        return status;
    } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        LOG << "FFMpegDecode::decode_video_packet output buffers changed";
        return status;
    } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        LOG << "FFMpegDecode::decode_video_packet output format changed";
        return status;
    } else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        //LOG << "FFMpegDecode::decode_video_packet no output buffer right now";
        return AVERROR(EAGAIN);
    } else {
        LOG << "FFMpegDecode::decodeVideoPacket unexpected info code: " << status;
        return -1;
    }
}

uint8_t* FFMpegDecodeMediaCodec::mediaCodecGetOutputBuffer(int status, size_t& size) {
    return AMediaCodec_getOutputBuffer(m_mediaCodec, status, &size);
}

void FFMpegDecodeMediaCodec::mediaCodecReleaseOutputBuffer(int status) {
    AMediaCodec_releaseOutputBuffer(m_mediaCodec, status, m_mediaCodecInfo.size != 0);
}

void FFMpegDecodeMediaCodec::clearResources() {
    if (m_mediaCodec) {
        AMediaCodec_stop(m_mediaCodec);
        AMediaCodec_delete(m_mediaCodec);
        m_mediaCodec = nullptr;
    }

    if (m_mediaExtractor) {
        AMediaExtractor_delete(m_mediaExtractor);
        m_mediaExtractor = nullptr;
    }

    if (m_bsfCtx) {
        av_bsf_free(&m_bsfCtx);
        m_bsfCtx = nullptr;
    }
}

}

#endif
