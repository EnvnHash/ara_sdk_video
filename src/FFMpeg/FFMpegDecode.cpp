/*
 * FFMpegDecode.cpp
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#ifdef ARA_USE_FFMPEG

#include "FFMpegDecode.h"

using namespace ara;
using namespace ara::av::ffmpeg;
using namespace glm;
using namespace std;

namespace ara::av {

static enum AVPixelFormat find_fmt_by_hw_type(const enum AVHWDeviceType type) {
    enum AVPixelFormat fmt;

    switch (type) {
        case AV_HWDEVICE_TYPE_VAAPI:
            fmt = AV_PIX_FMT_VAAPI;
            // LOG << "got hardware m_format AV_PIX_FMT_VAAPI";
            break;
        case AV_HWDEVICE_TYPE_DXVA2:
            fmt = AV_PIX_FMT_DXVA2_VLD;
            // LOG << "got hardware m_format AV_PIX_FMT_DXVA2_VLD";
            break;
        case AV_HWDEVICE_TYPE_D3D11VA:
            fmt = AV_PIX_FMT_D3D11;
            // LOG << "got hardware m_format AV_PIX_FMT_D3D11";
            break;
        case AV_HWDEVICE_TYPE_VDPAU:
            fmt = AV_PIX_FMT_VDPAU;
            // LOG << "got hardware m_format AV_PIX_FMT_VDPAU";
            break;
        case AV_HWDEVICE_TYPE_QSV:
            fmt = AV_PIX_FMT_QSV;
            //LOG << " got hardware m_format AV_HWDEVICE_TYPE_QSV ";
            break;
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            fmt = AV_PIX_FMT_VIDEOTOOLBOX;
            //LOG << " got hardware m_format AV_PIX_FMT_VIDEOTOOLBOX ";
            break;
        case AV_HWDEVICE_TYPE_MEDIACODEC:
            fmt = AV_PIX_FMT_MEDIACODEC;
            //LOG << " got hardware m_format AV_PIX_FMT_MEDIACODEC ";
            break;
        default:
            fmt = AV_PIX_FMT_NONE;
            break;
    }

    return fmt;
}


int FFMpegDecode::hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type) {
    int err = 0;

    if ((err = av_hwdevice_ctx_create(&m_hw_device_ctx, type, nullptr, nullptr, 0)) < 0) {
        LOGE <<  "Failed to create specified HW device.";
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(m_hw_device_ctx);

    return err;
}


static enum AVPixelFormat get_hw_format(AVCodecContext*, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    enum AVPixelFormat ret={AVPixelFormat(0)};
    bool gotFirst=false;
    bool found=false;

    for (p = pix_fmts; *p != -1; p++) {
        if (!gotFirst){
            ret = *p;
            gotFirst = true;
        }
        if (*p == FFMpegDecode::m_static_hwPixFmt) {
            ret = *p;
            found = true;
            break;
        }
    }

    if (!found)
        LOG << "FFMpegDecode Warning: Didn't find requested HW format. Took default instead";

    return ret;
}


int FFMpegDecode::OpenFile(GLBase *glbase, const std::string& filePath, int useNrThreads, int destWidth,
                           int destHeight, bool useHwAccel, bool decodeYuv420OnGpu, bool doStart, const std::function<void()>& initCb) {
    m_resourcesAllocated = false;
    m_filePath = filePath;
    m_destWidth = destWidth;
    m_destHeight = destHeight;
    m_destPixFmt = AV_PIX_FMT_BGRA;
    m_decodeYuv420OnGpu = decodeYuv420OnGpu;
    m_useHwAccel = useHwAccel;
    m_useNrThreads = useNrThreads;

#ifdef ARA_USE_GLBASE
    if (glbase){
        m_glbase = glbase;
        m_shCol = &glbase->shaderCollector();
    }
#endif

    avformat_network_init();
    m_logLevel = AV_LOG_VERBOSE;
    av_log_set_level(m_logLevel);

    // dumpDecoders();

    if (m_useHwAccel)
    {
#if defined(__linux__) && !defined(ANDROID)
        std::string hwDevType("vdpau");
#elif _WIN32
        std::string hwDevType("d3d11va");
#elif __APPLE__
        std::string hwDevType("videotoolbox");
        //m_hwDeviceType = av_hwdevice_find_type_by_name("qsv");
#elif __ANDROID__
        std::string hwDevType("mediacodec");
        //m_video_codec_name = "h264_mediacodec";
#endif
        m_hwDeviceType = av_hwdevice_find_type_by_name(hwDevType.c_str());

        if (m_hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
            LOGE << "Device type " << hwDevType << "  is not supported ";
            LOGE << "Available device types:";
            while((m_hwDeviceType = av_hwdevice_iterate_types(m_hwDeviceType)) != AV_HWDEVICE_TYPE_NONE)
                LOGE <<  av_hwdevice_get_type_name(m_hwDeviceType);
            return -1;
        } else {
            //LOG << "found hwDeviceType " << av_hwdevice_get_type_name(m_hwDeviceType);
        }

        m_hwPixFmt = find_fmt_by_hw_type(m_hwDeviceType);
        if (m_hwPixFmt == -1) {
            LOGE << "FFMpegDecode: Hardware acceleration " << hwDevType << " not support";
            return 0;
        }
    }

    av_log_set_callback( &ffmpeg::LogCallbackShim );	// custom logging

    // AVFormatContext holds the header information from the m_format (Container)
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        LOGE << "ERROR could not allocate memory for Format Context";
        return 0;
    }

    if (!av_dict_get(m_format_opts, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE))
    {
        av_dict_set(&m_format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        m_scan_all_pmts_set = 1;
    }

    av_dict_parse_string(&m_format_opts, "", ":", ",", 0);

    if (filePath.substr(0, 6) == "mms://" || filePath.substr(0, 7) == "mmsh://" ||
        filePath.substr(0, 7) == "mmst://" || filePath.substr(0, 7) == "mmsu://" ||
        filePath.substr(0, 7) == "http://" || filePath.substr(0, 8) == "https://" ||
        filePath.substr(0, 7) == "rtmp://" || filePath.substr(0, 6) == "udp://" ||
        filePath.substr(0, 7) == "rtsp://" || filePath.substr(0, 6) == "rtp://" ||
        filePath.substr(0, 6) == "ftp://" || filePath.substr(0, 7) == "sftp://" ||
        filePath.substr(0, 6) == "tcp://" || filePath.substr(0, 7) == "unix://" ||
        filePath.substr(0, 6) == "smb://")
    {
        m_is_stream = true;
        m_scan_all_pmts_set = 1;
        m_formatContext->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        if (filePath.substr(0, 7) == "rtmp://")
        {
            av_dict_set(&m_format_opts, "probesize", "64", 0); // with 64 sometimes errors (full hd + audio wrong samplerate)
            av_dict_set(&m_format_opts, "analyzeduration", "1", 0);
            //av_dict_set(&d, "sync", "ext", 0);

            m_hasNoTimeStamp = true;
            m_videoFrameBufferSize=24;
            m_nrFramesToStart=8;

        } else if (filePath.substr(0, 7) == "http://")
        {
            m_hasNoTimeStamp = true;
        }
    }

    if (doStart)
    {
        m_decodeThread = std::thread([this, initCb]
        {
            m_startTime = 0.0;
            m_run = true;
            setupStreams(nullptr, &m_format_opts, initCb);
            allocateResources();
            singleThreadDecodeLoop();
        });
        m_decodeThread.detach();
        return 1;
    } else {
        return setupStreams(nullptr, &m_format_opts, initCb);
    }
}

#ifdef __ANDROID__
int FFMpegDecode::OpenAndroidAsset(GLBase *glbase, struct android_app* app, std::string& assetName, int useNrThreads, int destWidth,
                           int destHeight, bool useHwAccel, bool decodeYuv420OnGpu, bool doStart, std::function<void()> initCb)
{
    m_resourcesAllocated = false;
    m_destWidth = destWidth;
    m_destHeight = destHeight;
    m_destPixFmt = AV_PIX_FMT_BGRA;
    m_decodeYuv420OnGpu = decodeYuv420OnGpu;
    m_useHwAccel = useHwAccel;
    m_useMediaCodec = useHwAccel;
    m_useNrThreads = useNrThreads;
    m_videoFrameBufferSize = useHwAccel ? 32 : 32;

#ifdef ARA_USE_GLBASE
    if (glbase){
        m_glbase = glbase;
        m_shCol = &glbase->shaderCollector();
    }
#endif

    avformat_network_init();
    m_logLevel = AV_LOG_INFO;
    av_log_set_level(m_logLevel);
    av_log_set_callback( &ffmpeg::LogCallbackShim );	// custom logging

    // AVFormatContext holds the header information from the m_format (Container)
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        LOGE << "ERROR could not allocate memory for Format Context";
        return -1;
    }

    if (!av_dict_get(m_format_opts, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE))
    {
        av_dict_set(&m_format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        m_scan_all_pmts_set = 1;
    }

    av_dict_parse_string(&m_format_opts, "", ":", ",", 0);

    if (assetName.empty())
        return -1;

    AAsset* assetDescriptor = AAssetManager_open(app->activity->assetManager, assetName.c_str(), AASSET_MODE_BUFFER);
    if (useHwAccel) initMediaCode(assetDescriptor);
    openAsset(assetDescriptor);

    if (doStart)
    {
        m_decodeThread = std::thread([this, initCb]
                                     {
                                         allocateResources();
                                         m_startTime = 0.0;
                                         m_run = true;
                                         setupStreams(nullptr, &m_format_opts, initCb);
                                         singleThreadDecodeLoop();
                                     });
        m_decodeThread.detach();
        return 1;
    } else
        return setupStreams(nullptr, &m_format_opts, initCb);
}


int FFMpegDecode::initMediaCode(AAsset* assetDescriptor)
{
    off_t outStart, outLen;
    int fd = AAsset_openFileDescriptor(assetDescriptor, &outStart, &outLen);

    m_mediaExtractor = AMediaExtractor_new();
    auto err = AMediaExtractor_setDataSourceFd(m_mediaExtractor, fd, static_cast<off64_t>(outStart),static_cast<off64_t>(outLen));
    close(fd);
    if (err != AMEDIA_OK) {
        LOGE << "FFmpegDecode::OpenAndroidAsset AMediaExtractor_setDataSourceFd fail. err=" << err;
        return -1;
    }

    int numTracks = AMediaExtractor_getTrackCount(m_mediaExtractor);

    LOG << "FFmpegDecode::OpenAndroidAsset AMediaExtractor_getTrackCount " << numTracks << " tracks";
    for (int i = 0; i < numTracks; i++)
    {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(m_mediaExtractor, i);
        auto s = AMediaFormat_toString(format);

        LOG << "FFmpegDecode::OpenAndroidAsset track " << i << " format:" << s;

        const char *mime;
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
            LOGE << "FFmpegDecode::OpenAndroidAsset no mime type";
            AMediaFormat_delete(format);
            format = nullptr;
            return -1;
        } else if (!strncmp(mime, "video/", 6))
        {
            // Omitting most error handling for clarity.
            // Production code should check for errors.
            AMediaExtractor_selectTrack(m_mediaExtractor, i);
            m_mediaCodec = AMediaCodec_createDecoderByType(mime);
            if (!m_mediaCodec) {
                LOGE << "FFmpegDecode::OpenAndroidAsset create media codec fail.";
                return -1;
            }
            AMediaCodec_configure(m_mediaCodec, format, nullptr, nullptr, 0);
            AMediaCodec_start(m_mediaCodec);
            AMediaFormat_delete(format);
            format = nullptr;
            return 0;
        }

        if (format)
            AMediaFormat_delete(format);
    }

    return 0;
}


int FFMpegDecode::openAsset(AAsset* assetDescriptor)
{
    size_t fileLength = AAsset_getLength(assetDescriptor);

    m_memInputBuf.resize(fileLength);
    int64_t readSize = AAsset_read(assetDescriptor, m_memInputBuf.data(), m_memInputBuf.size());

    AAsset_close(assetDescriptor);

    // Alloc a buffer for the stream
    auto fileStreamBuffer = (unsigned char*)av_malloc(m_avio_ctx_buffer_size);
    if (!fileStreamBuffer){
        LOGE << "OpenAndroidAsset failed out of memory";
        return -1;
    }

    m_memin_buffer.size = m_memInputBuf.size();
    m_memin_buffer.ptr = &m_memInputBuf[0];
    m_memin_buffer.start = &m_memInputBuf[0];
    m_memin_buffer.fileSize = fileLength;

    // Get a AVContext stream
    m_ioContext = avio_alloc_context(
            fileStreamBuffer,           // Buffer
            m_avio_ctx_buffer_size,     // Buffer size
            0,                          // Buffer is only readable - set to 1 for read/write
            &m_memin_buffer,          // User (your) specified data
            &FFMpegDecode::read_packet_from_inbuf,      // Function - Reading Packets (see example)
            0,                          // Function - Write Packets
            nullptr                     // Function - Seek to position in stream (see example)
    );

    if (!m_ioContext) {
        LOGE << "OpenAndroidAsset failed out of memory";
        return -1;
    }

    // Set up the Format Context
    m_formatContext->pb = m_ioContext;
    m_formatContext->flags |= AVFMT_FLAG_CUSTOM_IO; // we set up our own IO

   return 0;
}
#endif

int FFMpegDecode::read_packet_from_inbuf(void *opaque, uint8_t *buf, int buf_size) {
    auto bd = (struct memin_buffer_data*) opaque;
    buf_size = std::min<int>(buf_size, (int)bd->size);

    // loop
    if (!buf_size) {
        bd->ptr = bd->start;
    }

    // copy internal buffer data to buf
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;
    return buf_size;
}

int FFMpegDecode::OpenCamera(GLBase *glbase, const std::string& camName, int destWidth, int destHeight, bool decodeYuv420OnGpu) {
    m_resourcesAllocated = false;
#ifdef _WIN32
    m_filePath = "video="+camName;
#elif __linux__
    m_filePath = camName;
#endif
    m_destWidth = destWidth;
    m_destHeight = destHeight;
   // m_destPixFmt = AV_PIX_FMT_BGRA;
    m_destPixFmt = AV_PIX_FMT_BGR24;
    m_decodeYuv420OnGpu = decodeYuv420OnGpu;
    m_useHwAccel = false;
    m_useNrThreads = 2;
    m_hasNoTimeStamp = true;
    m_is_stream = true;
    m_videoFrameBufferSize = 2;

#ifdef ARA_USE_GLBASE
    if (glbase){
        m_glbase = glbase;
        m_shCol = &glbase->shaderCollector();
    }
#endif

    av_log_set_level(AV_LOG_VERBOSE);
    avdevice_register_all();

    av_log_set_callback( &ffmpeg::LogCallbackShim );	// custom logging

    // AVFormatContext holds the header information from the m_format (Container)
    // Allocating memory for this component
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        LOGE << "ERROR could not allocate memory for Format Context";
        return 0;
    }

#ifdef _WIN32
    const AVInputFormat* camInputFormat = av_find_input_format("dshow");
    if (!camInputFormat)
        LOGE << "ERROR couldn't find input m_format dshow";
#elif __ANDROID__
    AVInputFormat* camInputFormat = (AVInputFormat*) av_find_input_format("android_camera");
    if (!camInputFormat)
        LOGE << "ERROR couldn't find input m_format android_camera";

    av_dict_set(&m_format_opts, "video_size", "720x1280", 0);
    av_dict_set_int(&m_format_opts, "camera_index",0, 0);
    av_dict_set_int(&m_format_opts, "input_queue_size", 1, 0);

#elif __linux__
    auto camInputFormat = (AVInputFormat*) av_find_input_format("v4l2");
    if (!camInputFormat)
        LOGE << "ERROR couldn't find input m_format dshow";
#elif __APPLE__
    AVInputFormat* camInputFormat = (AVInputFormat*)av_find_input_format("avfoundation");
    if (!camInputFormat)
        LOGE << "ERROR couldn't find input m_format dshow";
#endif

    return setupStreams(camInputFormat, &m_format_opts, nullptr);
}

int FFMpegDecode::setupStreams(const AVInputFormat* format, AVDictionary** options, const std::function<void()>& initCb) {
    int err, ret;

    if (!m_formatContext) {
        return 0;
    }

    if ((err = avformat_open_input(&m_formatContext, !m_filePath.empty() ? m_filePath.c_str() : nullptr, format, options) != 0)) {
        LOGE << "FFMpegDecode::setupStreams ERROR could not open the file " << m_filePath << " " << err2str(err);
        return 0;
    }

    if (m_scan_all_pmts_set) {
        av_dict_set(&m_format_opts, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE);
    }

    AVDictionaryEntry *t{};
    if ((t = av_dict_get(m_format_opts, "", nullptr, AV_DICT_IGNORE_SUFFIX))) {
        LOGE << "Option " << t->key << " not found.";
        return 0;
    }

    if (m_genpts) {
        m_formatContext->flags |= AVFMT_FLAG_GENPTS;
    }

    av_format_inject_global_side_data(m_formatContext);

    // read Packets from the Format to get stream information
    // this function populates m_formatContext->streams
    // (of size equals to m_formatContext->nb_streams)
    // the arguments are: the AVFormatContext and options contains options for codec corresponding to i-th stream.
    // On return each dictionary will be filled with options that were not found.

    auto opts = setup_find_stream_info_opts(m_formatContext, m_codec_opts);
    auto orig_nb_streams = static_cast<int32_t>(m_formatContext->nb_streams);

    err = avformat_find_stream_info(m_formatContext, opts);

    for (auto i = 0; i < orig_nb_streams; ++i) {
        av_dict_free(&opts[i]);
    }
    av_freep(&opts);

    if (err < 0) {
        LOG << m_filePath << " could not find codec parameters";
        return 0;
    }

    if (m_formatContext->pb) {
        m_formatContext->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    }

    if (m_seek_by_bytes < 0) {
        m_seek_by_bytes = !!(m_formatContext->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", m_formatContext->iformat->name) != 0;
    }

    // if seeking requested, we execute it
    if (!m_is_stream && m_start_time != AV_NOPTS_VALUE) {
        int64_t timestamp = m_start_time;
        // add the stream start time
        if (m_formatContext->start_time != AV_NOPTS_VALUE) {
            timestamp += m_formatContext->start_time;
        }
        ret = avformat_seek_file(m_formatContext, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            LOG << m_filePath <<  ": could not seek to position " <<  (double)timestamp / AV_TIME_BASE;
        }
    }

    if (!m_filePath.empty()) {
        av_dump_format(m_formatContext, 0, m_filePath.c_str(), 0);
    }

    // the component that knows how to enCOde and DECode the stream, it's the codec (audio or video) http://ffmpeg.org/doxygen/trunk/structAVCodec.html
    m_video_stream_index = -1;
    m_audio_stream_index = -1;

    // loop though all the streams and print its main information
    for (auto i = 0; i < m_formatContext->nb_streams; ++i) {
        auto pLocalCodecParameters = m_formatContext->streams[i]->codecpar;
        if (!pLocalCodecParameters) continue;

        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO && m_forceAudioCodec) {
            pLocalCodecParameters->codec_id = m_forceAudioCodec;
        }

        // finds the registered decoder for a codec ID
        auto pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        if (pLocalCodec) {
            if (pLocalCodec->pix_fmts && pLocalCodec->pix_fmts[0] != -1) {
                int ind = 0;
                while (pLocalCodec->pix_fmts[ind] != -1) {
                    LOG <<  "CODEC possible pix_fmts: " << pLocalCodec->pix_fmts[ind];
                    ++ind;
                }
            }
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_video_stream_index = i;
            ++m_video_nr_tracks;

            m_video_codec_ctx = avcodec_alloc_context3(nullptr);
            if (!m_video_codec_ctx) {
                LOGE << "failed to allocated memory for video  AVCodecContext";
                return 0;
            }

            // set the number of threads here
            if (!m_useHwAccel) {
                m_video_codec_ctx->thread_count = m_useNrThreads;
            }

            // Fill the codec context based on the values from the supplied codec parameters
            if (avcodec_parameters_to_context(m_video_codec_ctx, pLocalCodecParameters) < 0) {
                LOGE << "failed to copy codec params to video codec context";
                return 0;
            }

            auto video_codec = avcodec_find_decoder(m_video_codec_ctx->codec_id);

            // optionally forcing a specific codec type
            if (m_video_codec_name) {
                video_codec = (AVCodec *) avcodec_find_decoder_by_name(m_video_codec_name);
                avcodec_free_context(&m_video_codec_ctx);
                m_video_codec_ctx = avcodec_alloc_context3(video_codec);
            }

            if (!video_codec) {
                if (m_video_codec_name) {
                    LOG << "No codec could be found with name " << m_video_codec_name;
                } else {
                    LOG << "No decoder could be found for codec " << avcodec_get_name(m_video_codec_ctx->codec_id);
                }

                ret = AVERROR(EINVAL);
                return -1;
            }

            m_video_codec_ctx->pkt_timebase = m_formatContext->streams[i]->time_base;
            m_video_codec_ctx->codec_id = video_codec->id;

            AVDictionary *vopts = nullptr;

            if (!av_dict_get(vopts, "threads", nullptr, 0))
                av_dict_set(&vopts, "threads", "auto", 0);

            //av_dict_set(&vopts, "refcounted_frames", "1", 0);

            if (m_useHwAccel && !m_useMediaCodec) {
                m_video_codec_ctx->get_format = get_hw_format;
                av_opt_set_int(m_video_codec_ctx, "refcounted_frames", 1, 0);    // what does this do?

                m_static_hwPixFmt = m_hwPixFmt;
                if (hw_decoder_init(m_video_codec_ctx, m_hwDeviceType) < 0) {
                    LOGE << "hw_decoder_init failed";
                    return 0;
                }

                m_hwPixFmt = m_static_hwPixFmt;
            }

            // save basic codec parameters for access from outside
            m_srcPixFmt = m_video_codec_ctx->pix_fmt;
            m_srcWidth = m_video_codec_ctx->width;
            m_srcHeight = m_video_codec_ctx->height;
            m_bitCount = m_video_codec_ctx->bits_per_raw_sample;

            //LOG << "m_srcWidth " <<  m_srcWidth << ", m_srcHeight " << m_srcHeight;
            //LOG << "AVStream->time_base before open coded " <<  m_formatContext->streams[i]->time_base.num << ", " << m_formatContext->streams[i]->time_base.den;
            //LOG << "AVStream->r_frame_rate before open coded " <<  m_formatContext->streams[i]->r_frame_rate.num << ", " << m_formatContext->streams[i]->r_frame_rate.den;
            //LOG << "AVStream->start_time " << m_formatContext->streams[i]->start_time;
            //LOG << "AVStream->duration " << m_formatContext->streams[i]->duration;
            //LOG << "m_bitCount " << m_bitCount;
            //LOG << "";

            m_timeBaseDiv = static_cast<double>(m_formatContext->streams[i]->time_base.num) /
                            static_cast<double>(m_formatContext->streams[i]->time_base.den);

            if (m_formatContext->streams[i]->r_frame_rate.num) {
                m_frameDur = static_cast<double>(m_formatContext->streams[i]->r_frame_rate.den) /
                             static_cast<double>(m_formatContext->streams[i]->r_frame_rate.num);
            } else {
                m_frameDur = 0.0;
            }

            m_fps = static_cast<int32_t>((static_cast<double>(m_formatContext->streams[i]->r_frame_rate.num) /
                                          static_cast<double>(m_formatContext->streams[i]->r_frame_rate.den)));

            if (m_decodeYuv420OnGpu || !m_destWidth) {
                m_destWidth = m_srcWidth;
            }

            if (m_decodeYuv420OnGpu || !m_destHeight) {
                m_destHeight = m_srcHeight;
            }

            // Initialize the AVCodecContext to use the given AVCodec.
            if ((ret = avcodec_open2(m_video_codec_ctx, video_codec, reinterpret_cast<AVDictionary **>(&opts))) < 0) {
#ifdef __linux__
                LOGE << "failed to open video codec through avcodec_open2 " << av_make_error_string(ret);
#endif
                return 0;
            }

#ifdef __ANDROID__
            m_bsf = (AVBitStreamFilter*) av_bsf_get_by_name((char*)"h264_mp4toannexb");
            if(!m_bsf){
                LOGE << "bitstreamfilter not found";
                return AVERROR_BSF_NOT_FOUND;
            }
            if ((ret = av_bsf_alloc(m_bsf, &m_bsfCtx)))
                return ret;
            if (((ret = avcodec_parameters_from_context(m_bsfCtx->par_in, m_video_codec_ctx)) < 0) ||
                ((ret = av_bsf_init(m_bsfCtx)) < 0)) {
                av_bsf_free(&m_bsfCtx);
                LOGE << "av_bsf_init failed";
                return ret;
            }
#endif

            AVDictionaryEntry* dictEntr = nullptr;
            if ((dictEntr = av_dict_get(vopts, "", nullptr, AV_DICT_IGNORE_SUFFIX))) {
                LOGE << "Option " << dictEntr->key << "not found";
            }
        } else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (m_forceSampleRate != 0) {
                pLocalCodecParameters->sample_rate = m_forceSampleRate;
            }

            if (m_forceNrChannels != 0) {
                pLocalCodecParameters->channels = m_forceNrChannels;
            }

            // TODO : there is a problem with 5.1, AAC which is detected as 1 channel
            m_audio_nr_channels = pLocalCodecParameters->channels;
            m_audio_stream_index = i;
            m_audio_codec = (AVCodec*)pLocalCodec;

            // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
            m_audio_codec_ctx = avcodec_alloc_context3(pLocalCodec);
            if (!m_audio_codec_ctx) {
                LOGE << "failed to allocated memory for audio  AVCodecContext";
                return 0;
            }

            // Fill the codec context based on the values from the supplied codec parameters
            if (avcodec_parameters_to_context(m_audio_codec_ctx, pLocalCodecParameters) < 0) {
                LOGE << "failed to copy codec params to audio codec context";
                return 0;
            }

            // Initialize the AVCodecContext to use the given AVCodec.
            if (avcodec_open2(m_audio_codec_ctx, m_audio_codec, nullptr) < 0) {
                LOGE << "failed to open audio codec through avcodec_open2";
                return 0;
            }
        }
    }

    if (initCb) {
        initCb();
    }

    return 1;
}

int FFMpegDecode::allocateResources() {
    m_packet = av_packet_alloc();
    if (!m_packet) {
        LOGE << "failed to allocated memory for AVPacket";
        return 0;
    }

    if (m_destWidth && m_destHeight) {
        m_framePtr = std::vector<AVFrame*>(m_videoFrameBufferSize);
        for (auto &it : m_framePtr) {
            it = av_frame_alloc();
            it->width = m_destWidth;
            it->height = m_destHeight;
            it->pts = -1;
            if (!it) {
                LOGE << "failed to allocated memory for AVFrame";
                return 0;
            }
        }
    }

    m_frame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();

    if (m_video_codec_ctx) {
#ifdef ARA_USE_GLBASE
        if (!m_decodeYuv420OnGpu && m_destWidth && m_destHeight) {
            m_buffer = std::vector<std::vector<uint8_t>>(m_videoFrameBufferSize);
            m_bgraFrame = std::vector<AVFrame*>(m_videoFrameBufferSize);
            for (uint32_t i = 0; i < m_videoFrameBufferSize; i++)
                m_bgraFrame[i] = alloc_picture(m_destPixFmt, m_destWidth, m_destHeight, m_buffer.begin() +i);
        }

        // destFmt BGRA
        if (m_usePbos) {
            m_pbos = std::vector<GLuint>(m_nrPboBufs);
            std::fill(m_pbos.begin(), m_pbos.end(), 0);
        }
#endif
    }

    m_ptss = std::vector<double>(m_videoFrameBufferSize);
    std::fill(m_ptss.begin(), m_ptss.end(), -1.0);

    m_totNumFrames = (uint) get_total_frames();
    m_resourcesAllocated = true;

    return 1;
}

AVDictionary** FFMpegDecode::setup_find_stream_info_opts(AVFormatContext *s, AVDictionary *codec_opts) {
    if (!s->nb_streams) {
        return nullptr;
    }

    AVDictionary **opts{};
    opts = (AVDictionary**) av_calloc(s->nb_streams, sizeof(*opts));
    if (!opts) {
        LOGE << "Could not alloc memory for stream options.";
        return nullptr;
    }

    for (unsigned int i = 0; i < s->nb_streams; i++) {
        if (!s->streams[i]->codecpar){
            LOGE << "FFMpegDecode::setup_find_stream_info_opts Error streams[i]->codecpar == null";
            continue;
        }
        opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id, s, s->streams[i], nullptr);
    }

    return opts;
}

AVDictionary* FFMpegDecode::filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s,
                                              AVStream *st, AVCodec *codec) {
    AVDictionary    *ret = nullptr;
    AVDictionaryEntry *t = nullptr;
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                                      : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec) {
        codec = (AVCodec*)(s->oformat ? avcodec_find_encoder(codec_id) : avcodec_find_decoder(codec_id));
    }

    switch (st->codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            prefix  = 'v';
            flags  |= AV_OPT_FLAG_VIDEO_PARAM;
            break;
        case AVMEDIA_TYPE_AUDIO:
            prefix  = 'a';
            flags  |= AV_OPT_FLAG_AUDIO_PARAM;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            prefix  = 's';
            flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
            break;
        default:
            break;
    }

    while ((t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX))) {
        char *p = strchr(t->key, ':');

        // check stream specification in m_opt name
        if (p) {
            switch (check_stream_specifier(s, st, p + 1)) {
                case  1: *p = 0; break;
                case  0:         continue;
                default:         break;
            }
        }

        if (av_opt_find(&cc, t->key, nullptr, flags, AV_OPT_SEARCH_FAKE_OBJ) || !codec ||
            (codec->priv_class && av_opt_find(&codec->priv_class, t->key, nullptr, flags, AV_OPT_SEARCH_FAKE_OBJ))) {
            av_dict_set(&ret, t->key, t->value, 0);
        } else if (t->key[0] == prefix && av_opt_find(&cc, t->key + 1, nullptr, flags, AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&ret, t->key + 1, t->value, 0);
        }

        if (p) {
            *p = ':';
        }
    }
    return ret;
}

int FFMpegDecode::check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec) {
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    }
    return ret;
}

#ifdef ARA_USE_GLBASE
void FFMpegDecode::initShader(AVPixelFormat srcPixFmt) {
    if (m_decodeYuv420OnGpu && !m_shCol->hasShader("FFMpegDecode_yuv")) {
        auto shdr_Header = ara::ShaderCollector::getShaderHeader();

        std::string vert = STRINGIFY( layout(location = 0) in vec4 position; \n
            layout(location = 2) in vec2 texCoord; \n
            uniform mat4 m_pvm; \n
            out vec2 tex_coord; \n
            void main() {\n
                \t tex_coord = texCoord; \n
                \t gl_Position = m_pvm * position; \n
        });
        vert = shdr_Header + "// yuv420 texture shader, vert\n"  + vert;

        // YUV420 is a planar (non-packed) m_format.
        // the first plane is the Y with one byte per pixel.
        // the second plane us U with one byte for each 2x2 square of pixels
        // the third plane is V with one byte for each 2x2 square of pixels
        //
        // tex_unit - contains the Y (luminance) component of the
        //    image. this is a texture unit set up by the OpenGL program.
        // u_texture_unit, v_texture_unit - contain the chrominance parts of
        //    the image. also texture units  set up by the OpenGL program.
        std::string frag = STRINGIFY(uniform sampler2D tex_unit; \n // Y component
        uniform sampler2D u_tex_unit; \n // U component
        uniform sampler2D v_tex_unit; \n // V component
        uniform float alpha; \n // V component
        \n
        in vec2 tex_coord; \n
        layout(location = 0) out vec4 fragColor; \n
        void main() { \n);

        // NV12
        if (srcPixFmt == AV_PIX_FMT_NV12) {
            frag += STRINGIFY(float y = texture(tex_unit, tex_coord).r; \n
                                      float u = texture(u_tex_unit, tex_coord).r - 0.5; \n
                                      float v = texture(u_tex_unit, tex_coord).g - 0.5; \n

                                      fragColor = vec4(
                              (vec3(y + 1.4021 * v, \n
                                      y - 0.34482 * u - 0.71405 * v, \n
                                      y + 1.7713 * u)
                                      - 0.05) * 1.07, \n
                                      alpha); \n
            );
        } else if (srcPixFmt == AV_PIX_FMT_NV21) {
            frag += STRINGIFY(float y = texture(tex_unit, tex_coord).r; \n
                                      float u = texture(u_tex_unit, tex_coord).g - 0.5; \n
                                      float v = texture(u_tex_unit, tex_coord).r - 0.5; \n

                                      fragColor = vec4(
                              (vec3(y + 1.4021 * v, \n
                                      y - 0.34482 * u - 0.71405 * v, \n
                                      y + 1.7713 * u)
                                      - 0.05) * 1.07, \n
                                      alpha); \n
            );
        } else {
            // YUV420P
            frag += STRINGIFY(
                    float y = texture(tex_unit, tex_coord).r; \n
                    float u = texture(u_tex_unit, tex_coord).r - 0.5; \n
                    float v = texture(v_tex_unit, tex_coord).r - 0.5; \n

                    float r = y + 1.402 * v; \n
                    float g = y - 0.344 * u - 0.714 * v; \n
                    float b = y + 1.772 * u; \n

                    fragColor = vec4(vec3(r, g, b), alpha); \n);
        }

        frag += "}";
        frag = shdr_Header + "// YUV420 fragment shader\n"  + frag;
        m_shader = m_shCol->add("FFMpegDecode_yuv", vert, frag);
    } else {
        m_shader = m_shCol->getStdTexAlpha();
    }
}

void FFMpegDecode::shaderBegin() {
    if (m_run && m_shader && !m_textures.empty()) {
        m_shader->begin();
        m_shader->setIdentMatrix4fv("m_pvm");
        m_shader->setUniform1f("alpha", 1.f); // y

        if (m_decodeYuv420OnGpu) {
            m_shader->setUniform1i("tex_unit", 0); // y
            m_shader->setUniform1i("u_tex_unit", 1); // u
            m_shader->setUniform1i("v_tex_unit", 2); // v

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId()); // y

            if (m_srcPixFmt == AV_PIX_FMT_YUV420P || m_srcPixFmt == AV_PIX_FMT_NV12) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_textures[1]->getId()); // u
            }

            if (m_srcPixFmt == AV_PIX_FMT_YUV420P) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, m_textures[2]->getId()); // v
            }
        } else {
            m_shader->setUniform1i("tex", 0); // y
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId()); // y
        }
    }
}

void FFMpegDecode::shaderEnd() const {
    if (m_run && m_decodeYuv420OnGpu) {
        ara::Shaders::end();
    }
}
#endif

void FFMpegDecode::start(double time) {
    allocateResources();
    m_run = true;
    m_decodeThread = std::thread([&]{ singleThreadDecodeLoop();});
    m_decodeThread.detach();
}

void FFMpegDecode::stop() {
    m_run = false;
    m_decodeCond.notify();     // unlock waits
    bool unlock = m_mutex.try_lock();
    m_endThreadCond.wait(0);
    clearResources();
    if (unlock) {
        m_mutex.unlock();
    }
}

void FFMpegDecode::alloc_gl_res(AVPixelFormat srcPixFmt) {
#ifdef ARA_USE_GLBASE
    initShader(srcPixFmt);

    if (m_decodeYuv420OnGpu) {
        m_nrTexBuffers = (srcPixFmt == AV_PIX_FMT_NV12 || srcPixFmt == AV_PIX_FMT_NV21) ? 2 : 3;
    } else {
        m_nrTexBuffers = 1;
    }

    m_textures = std::vector<std::unique_ptr<Texture>>(m_nrTexBuffers);
    for (auto &it : m_textures) {
        it = make_unique<Texture>(m_glbase);
    }

    if (m_decodeYuv420OnGpu) {
        if (m_srcPixFmt == AV_PIX_FMT_NV12 || m_srcPixFmt == AV_PIX_FMT_NV21) {
            m_textures[0]->allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_RG8, GL_RG, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        } else {   // YUV420P
            m_textures[0]->allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[2]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        }
    } else {
        m_textures[0]->allocate2D(m_destWidth, m_destHeight, GL_RGB8, GL_RGB, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
    }
#endif
}

AVFrame* FFMpegDecode::alloc_picture(enum AVPixelFormat pix_fmt, int width, int height, vector<vector<uint8_t>>::iterator it) {
    auto picture = av_frame_alloc();
    if (!picture) {
        return nullptr;
    }

    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;
    picture->pts = -1;

    // Allocate memory for the raw data we get when converting.
    (*it) = vector<uint8_t>( av_image_get_buffer_size(pix_fmt, width, height, 1) );

    // Assign appropriate parts of buffer to image planes in m_inpFrame
    av_image_fill_arrays(picture->data, picture->linesize, &(*it)[0], pix_fmt, width, height, 1);
    return picture;
}

bool FFMpegDecode::setAudioConverter(int destSampleRate, AVSampleFormat format)
{
    m_useAudioConversion = true;

    if (!m_audio_codec_ctx){
        LOGE << "FFMpegDecode::setAudioConverter failed!, m_audio_codec_ctx == NULL";
        return false;
    }
    m_dstChannelLayout = m_audio_codec_ctx->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    m_dstSampleRate = destSampleRate;
    m_dst_audio_nb_channels = m_dstChannelLayout == AV_CH_LAYOUT_MONO ? 1 : 2;
    m_dst_sample_fmt = format;

    // create resampler context
    m_audio_swr_ctx = swr_alloc();
    if (!m_audio_swr_ctx) {
        LOGE << "Could not allocate resampler context";
        //ret = AVERROR(ENOMEM);
        return false;
    }

    // If you don't know the channel layout, get it from the number of channels.
    if (m_audio_codec_ctx->channel_layout == 0)
        m_audio_codec_ctx->channel_layout = av_get_default_channel_layout( m_audio_codec_ctx->channels );

    // set options
    av_opt_set_int(m_audio_swr_ctx, "in_channel_layout",    static_cast<int64_t>(m_audio_codec_ctx->channel_layout), 0);
    av_opt_set_int(m_audio_swr_ctx, "in_sample_rate",       m_audio_codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(m_audio_swr_ctx, "in_sample_fmt", m_audio_codec_ctx->sample_fmt, 0);

    av_opt_set_int(m_audio_swr_ctx, "out_channel_layout",    m_dstChannelLayout, 0);
    av_opt_set_int(m_audio_swr_ctx, "out_sample_rate",       m_dstSampleRate, 0);
    av_opt_set_sample_fmt(m_audio_swr_ctx, "out_sample_fmt", m_dst_sample_fmt, 0);

    // initialize the resampling context
    if (swr_init(m_audio_swr_ctx) < 0) {
        LOGE << "Failed to initialize the resampling context";
        return false;
    }

    return true;
}

void FFMpegDecode::singleThreadDecodeLoop() {
    // decode packet
    while (m_run) {
        if (m_formatContext && m_packet && !m_pause) {
            // in case the queue is filled, don't read more frames
            if ((m_video_stream_index > -1
                && static_cast<int32_t>(m_nrBufferedFrames) >= static_cast<int32_t>(m_videoFrameBufferSize))
                || m_audioQueueFull) {
                this_thread::sleep_for(500us);
                continue;
            }

            if (av_read_frame(m_formatContext, m_packet) < 0) {
                continue;
            }

            // if it's the video stream and the m_buffer queue is not filled
            if (m_packet->stream_index == m_video_stream_index) {
                // we are using multiple frames, so the frames reaching here are not
                // in a continuous order!!!!!!
                m_actFrameNr = (uint) ((double) m_packet->pts * m_timeBaseDiv / m_frameDur);

                if ((m_totNumFrames - 1) == m_actFrameNr && m_loop && !m_is_stream) {
                    av_seek_frame(m_formatContext, m_video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
                }

                if (decode_video_packet(m_packet, m_video_codec_ctx) < 0) {
                    continue;
                }
            } else if (m_packet->stream_index == m_audio_stream_index
                       && decode_audio_packet(m_packet, m_audio_codec_ctx) < 0) {
                continue;
            }

            av_packet_unref(m_packet);
        } else {
            this_thread::sleep_for(1000us);
        }
    }

    m_endThreadCond.notify();	 // wait until the packet was needed
}

int FFMpegDecode::decode_video_packet(AVPacket* packet, AVCodecContext* codecContext) {
    if (!codecContext) {
        return 0;
    }

    int response = -1;

    if (!m_useMediaCodec){
        response = avcodec_send_packet(codecContext, packet);
        if (response < 0) {
            return response;
        }
    } else {
#ifdef __ANDROID__
        response = mediaCodecGetInputBuffer(packet);
#endif
    }

    while (m_run && response >= 0) {
        if (m_run) {
            if (!m_useMediaCodec && m_useHwAccel) {
                response = avcodec_receive_frame(codecContext, m_frame);            // always calls av_frame_unref
            } else if (m_useMediaCodec && m_useHwAccel){
#ifdef __ANDROID__
                response = mediaCodecDequeueOutputBuffer();
#endif
            } else {
                m_mutex.lock();
                response = avcodec_receive_frame(codecContext, m_framePtr[m_decFramePtr]);    // always calls av_frame_unref
                m_mutex.unlock();
            }

            if (response == AVERROR(EAGAIN)) {
                break;
            } else if (response == AVERROR_EOF) {
                LOGE <<  "end of file";
            } else if (response < 0) {
                return response;
            }

            // we got a valid packet!!
            if (m_run && response >= 0) {
                if (!m_decodeYuv420OnGpu) {
                    m_mutex.lock();

                    // convert frame to desired size and m_format
                    if (m_useHwAccel && !m_useMediaCodec && m_frame->format == m_hwPixFmt) {
                        // retrieve data from GPU to CPU, dst m_frame must be "clean"
                        if (av_hwframe_transfer_data(m_framePtr[m_decFramePtr], m_frame, 0) < 0) {
                            LOGE << "Error transferring the data to system memory";
                        }

                        m_framePtr[m_decFramePtr]->pts = m_frame->pts;
                        m_framePtr[m_decFramePtr]->pkt_size = m_frame->pkt_size;
                        m_framePtr[m_decFramePtr]->coded_picture_number = m_frame->coded_picture_number;
                        m_framePtr[m_decFramePtr]->pict_type = m_frame->pict_type;
                    } else if (m_useHwAccel && m_useMediaCodec) {
#ifdef __ANDROID__
                        size_t hwBufSize;
                        auto buffer = mediaCodecGetOutputBuffer(response, hwBufSize);

                        // LOG << m_rawBuffer[m_decFramePtr].size();
                        memcpy(&m_framePtr[m_decFramePtr]->data[0][0], buffer, m_rawBuffer[m_decFramePtr].size());

                        m_framePtr[m_decFramePtr]->pts = m_mediaCodecInfo.presentationTimeUs * av_q2d(m_formatContext->streams[m_video_stream_index]->time_base) * 1000;

                        m_framePtr[m_decFramePtr]->pkt_size = packet->size;
                        m_framePtr[m_decFramePtr]->format = (AVPixelFormat) codecContext->pix_fmt;
                        //m_framePtr[m_decFramePtr]->coded_picture_number = packet->coded_picture_number;
                        //m_framePtr[m_decFramePtr]->pict_type = packet->pi;

                        mediaCodecReleaseOutputBuffer(response);
#endif
                    }

                    // since now for the first time we are really sure about the pix_fmt the decode
                    // frame will have, initialize the textures and the swscale context if necessary
                    if (!m_img_convert_ctx && !m_decodeYuv420OnGpu) {
                        m_img_convert_ctx = sws_getCachedContext(m_img_convert_ctx,
                                                                 codecContext->width, codecContext->height,
                                                                 (AVPixelFormat) m_framePtr[m_decFramePtr]->format,
                                                                 m_destWidth, m_destHeight, m_destPixFmt,
                                                                 SWS_FAST_BILINEAR, //SWS_BICUBIC,
                                                                 nullptr, nullptr, nullptr);
                    }

                    response = sws_scale(m_img_convert_ctx,
                                         m_framePtr[m_decFramePtr]->data, m_framePtr[m_decFramePtr]->linesize, 0,
                                         codecContext->height,
                                         m_bgraFrame[m_decFramePtr]->data, m_bgraFrame[m_decFramePtr]->linesize);

                    if (m_decodeCb) {
                        m_decodeCb(m_bgraFrame[m_decFramePtr]->data[0]);
                    }

                    if (response < 0) {
                        LOGE << "FFMpegDecode ERROR, sws_scale failed!!!";
                    }

                    m_mutex.unlock();
                } else {
                    if (m_useHwAccel && !m_useMediaCodec && (m_frame->format > -1) && (m_frame->format == m_hwPixFmt)) {
                        m_mutex.lock();

                        // retrieve data from GPU to CPU, dst frame must be "clean"
                        if (av_hwframe_transfer_data(m_framePtr[m_decFramePtr], m_frame, 0) < 0) {
                            LOGE << "Error transferring the data to system memory";
                        }

                        // not all parameters are copied, so, do this manually
                        m_framePtr[m_decFramePtr]->pts = m_frame->pts;
                        m_framePtr[m_decFramePtr]->pkt_size = m_frame->pkt_size;
                        m_framePtr[m_decFramePtr]->coded_picture_number = m_frame->coded_picture_number;
                        m_framePtr[m_decFramePtr]->pict_type = m_frame->pict_type;

                        m_srcPixFmt = (AVPixelFormat) m_framePtr[m_decFramePtr]->format;
                        m_mutex.unlock();

                    } else if(m_useHwAccel && m_useMediaCodec) {
#ifdef __ANDROID__
                        m_mutex.lock();

                        size_t hwBufSize;
                        auto buffer = mediaCodecGetOutputBuffer(response, hwBufSize);

                        // LOG << m_rawBuffer[m_decFramePtr].size();
                        memcpy(&m_framePtr[m_decFramePtr]->data[0][0], buffer, m_rawBuffer[m_decFramePtr].size());

                        m_framePtr[m_decFramePtr]->pts = m_mediaCodecInfo.presentationTimeUs;
                        m_framePtr[m_decFramePtr]->format = (AVPixelFormat) codecContext->pix_fmt;
                        //m_framePtr[m_decFramePtr]->pkt_size = packet->size;
                        //m_framePtr[m_decFramePtr]->coded_picture_number = packet->coded_picture_number;
                        //m_framePtr[m_decFramePtr]->pict_type = packet->pi;

                        mediaCodecReleaseOutputBuffer(response);

                        m_mutex.unlock();
#endif
                    } else {
                        m_srcPixFmt = (AVPixelFormat) codecContext->pix_fmt;
                    }
                }

                if (m_videoCb) {
                    m_videoCb(m_framePtr[m_decFramePtr]);
                }

                m_mutex.lock();

                m_ptss[m_decFramePtr] = m_timeBaseDiv * (double) m_framePtr[m_decFramePtr]->pts;

                // the stream might start with a pts different from 0, for this reason here register explicitly the starting pts
                if (m_nrBufferedFrames == 0) {
                    m_videoStartPts = (m_useHwAccel && !m_useMediaCodec) ? m_frame->pts
                                                                         : m_framePtr[m_decFramePtr]->pts;
                }

                // call end callback if we are done
                if (m_endCb && m_ptss[m_decFramePtr] < m_lastPtss && m_lastPtss > 0) {
                    m_endCb();
                }

                m_lastPtss = m_ptss[m_decFramePtr];

                if (!m_gotFirstVideoFrame) {
                    m_gotFirstVideoFrame = true;
                    if (m_firstVideoFrameCb) {
                        m_firstVideoFrameCb();
                    }
                }

                m_nrBufferedFrames++;
                //LOG <<  "++ " << m_nrBufferedFrames;

                m_decFramePtr = ++m_decFramePtr % m_videoFrameBufferSize;
                m_mutex.unlock();
                response = -1; // break loop
            }
        }
    }

    return 0;
}

#ifdef __ANDROID__
int FFMpegDecode::mediaCodecGetInputBuffer(AVPacket* packet)
{
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


int FFMpegDecode::mediaCodecDequeueOutputBuffer()
{
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
        LOG << "FFMpegDecode::decode_video_packet unexpected info code: " << status;
        return -1;
    }
}


uint8_t* FFMpegDecode::mediaCodecGetOutputBuffer(int status, size_t& size)
{
    return AMediaCodec_getOutputBuffer(m_mediaCodec, status, &size);
}


void FFMpegDecode::mediaCodecReleaseOutputBuffer(int status)
{
    AMediaCodec_releaseOutputBuffer(m_mediaCodec, status, m_mediaCodecInfo.size != 0);
}
#endif

int FFMpegDecode::decode_audio_packet(AVPacket *packet, AVCodecContext *codecContext) {
    // Supply raw packet data as input to a decoder
    int response = avcodec_send_packet(codecContext, packet);
    if (response < 0) {
        return response;
    }

    while (m_run && response >= 0) {
        response = avcodec_receive_frame(codecContext, m_audioFrame);            // always calls av_frame_unref

        if (response == AVERROR(EAGAIN)) {
            break;
        } else if (response == AVERROR_EOF) {
            LOGE << "end of file";
        } else if (response < 0) {
            LOGE << "Error while receiving a m_frame from the decoder " << err2str(response);
            return response;
        }

        // we got a valid packet!!
        if (m_run && response >= 0) {
            int data_size = av_samples_get_buffer_size(nullptr, codecContext->channels,
                                                       m_audioFrame->nb_samples,
                                                       codecContext->sample_fmt, 1);

            // mp3 codec needs some m_frame to have valid data
            if (m_audio_codec_ctx->codec_id == AV_CODEC_ID_MP3 && data_size < 4096) {
                continue;
            }

            if (m_useAudioConversion) {
                // init the destination buffer if necessary
                if (!m_dst_sampleBuffer) {
                    m_max_dst_nb_samples = m_dst_nb_samples = (int) av_rescale_rnd(m_audioFrame->nb_samples, m_dstSampleRate,
                            m_audio_codec_ctx->sample_rate, AV_ROUND_UP);

                    // buffer is going to be directly written to a rawaudio file, no alignment
                    m_dst_audio_nb_channels = av_get_channel_layout_nb_channels(m_dstChannelLayout);
                    response = av_samples_alloc_array_and_samples((uint8_t***)&m_dst_sampleBuffer, &m_dst_audio_linesize,
                                                                  m_dst_audio_nb_channels, m_dst_nb_samples, m_dst_sample_fmt, 0);

                    if (response < 0) {
                        LOGE << "ERROR: could not allocate destination sample buffer";
                        break;
                    }
                }

                // convert to destination m_format
                response = swr_convert(m_audio_swr_ctx, m_dst_sampleBuffer, m_dst_nb_samples,
                        (const uint8_t**)m_audioFrame->data, m_audioFrame->nb_samples);

                if (response < 0) {
                    LOGE << "Error while converting";
                    break;
                }

                m_audioCbData.nChannels = m_dst_audio_nb_channels;
                m_audioCbData.samples = m_dst_nb_samples;
                m_audioCbData.byteSize = m_dst_audio_linesize;
                m_audioCbData.buffer = m_dst_sampleBuffer;
                m_audioCbData.sampleRate = m_dstSampleRate;
                m_audioCbData.sampleFmt = m_dst_sample_fmt;

                if (m_audioCb) {
                    m_audioCb(m_audioCbData);
                }
            } else {
                if (!m_dst_sampleBuffer) {
                    // buffer is going to be directly written to a raw audio, no alignment
                    response = av_samples_alloc_array_and_samples((uint8_t***)&m_dst_sampleBuffer, &m_audioFrame->linesize[0],
                                                                  m_audioFrame->channels, m_audioFrame->nb_samples,
                                                                  (AVSampleFormat)m_audioFrame->format, 0);
                    if (response < 0) {
                        LOGE << "FFmpegDecode: Error allocation audio buffer";
                        break;
                    }
                }

                if (m_audioCb) {
                    m_audioCb(m_audioCbData);
                }
            }

            if (!m_gotFirstAudioFrame) {
                m_gotFirstAudioFrame = true;
                if (m_firstAudioFrameCb) {
                    m_firstAudioFrameCb();
                }
            }

            if (response < 0) {
                LOGE << "FFMpegDecode ERROR, sws_scale failed!!!";
            }
        }
    }

    return 0;
}

uint8_t* FFMpegDecode::reqNextBuf() {
    uint8_t* buf=nullptr;

    if (m_resourcesAllocated && m_run && m_nrBufferedFrames >= 1) {
        ++m_frameToUpload %= m_videoFrameBufferSize;

        if (m_frameToUpload > -1
            && m_bgraFrame[m_frameToUpload]->width
            && m_bgraFrame[m_frameToUpload]->height
            && m_bgraFrame[m_frameToUpload]->data[0]) {
            buf = m_bgraFrame[m_frameToUpload]->data[0];

            // mark as consumed
            m_framePtr[m_frameToUpload]->pts = -1;

            if (m_nrBufferedFrames > 0) {
                --m_nrBufferedFrames;
            }

            m_decodeCond.notify();     // wait until the packet was needed
        }
    }

    return buf;
}

#ifdef ARA_USE_GLBASE
void FFMpegDecode::loadFrameToTexture(double time) {
    if (m_resourcesAllocated && m_run && !m_pause && m_nrBufferedFrames >= 1) {
        double actRelTime = time - m_startTime + ((double)m_videoStartPts * m_timeBaseDiv);
        uint searchInd = (m_frameToUpload +1) % m_videoFrameBufferSize;
        bool uploadNewFrame = false;

        if (!m_gl_res_inited && m_srcWidth && m_srcHeight) {
            alloc_gl_res(m_srcPixFmt);
            m_gl_res_inited = true;
        }

        if (!m_gl_res_inited) {
            return;
        }

        // check for the first frame or a frame with a pts close to the actual time
        if (!m_hasNoTimeStamp) {
            while (searchInd < (uint) m_videoFrameBufferSize) {
                if (!m_firstFramePresented) {
                    if ((m_ptss[searchInd % m_videoFrameBufferSize] == m_videoStartPts) || m_is_stream) {
                        m_firstFramePresented = true;
                        m_startTime = time;
                        m_frameToUpload = searchInd % m_videoFrameBufferSize;
                        m_consumeFrames = true;
                        uploadNewFrame = true;
                        break;
                    }
                } else {
                    // if we have a timestamp with a pts that differs less than 25% of a frame duration, present it
                    if (std::fabs(m_ptss[searchInd % m_videoFrameBufferSize] - actRelTime) < (m_frameDur * 0.25)
                        || (m_ptss[searchInd % m_videoFrameBufferSize] != -1.0 && m_ptss[searchInd % m_videoFrameBufferSize] < actRelTime)) {
                        // if we are playing in loop mode and reached the last m_frame, reset the m_startTime
                        if ((uint) (m_ptss[searchInd % m_videoFrameBufferSize] / m_frameDur) == (m_totNumFrames - 1)) {
                            m_startTime = time;
                        }

                        int newFrameInd = searchInd % m_videoFrameBufferSize;
                        uploadNewFrame = newFrameInd != m_frameToUpload;
                        m_frameToUpload = newFrameInd;
                        break;
                    }
                }

                // check if there are frames that are too old
                ++searchInd;
            }
        }
        else
        {
            // in case there is no timestamp just take the framerate to offset
            if (!m_consumeFrames && (int)m_nrBufferedFrames >= m_nrFramesToStart) {
                m_consumeFrames = true;
                m_startTime = time;
                actRelTime = 0.0;
            }

            uploadNewFrame = m_consumeFrames && m_nrBufferedFrames > 1 && (time - m_lastToGlTime >= (0.8f / m_fps));
            if (uploadNewFrame) {
                actRelTime = time - m_startTime;
                ++m_frameToUpload %= m_videoFrameBufferSize;

               /* // hypothetical frameNumber in relation to the actual time
                int newFrameNr = (int)(actRelTime * (double) m_fps) % m_videoFrameBufferSize;
                if (newFrameNr != m_frameToUpload)
                {
                    //LOG << "m_consumeFrames actRelTime " << actRelTime << " newFrameNr " << newFrameNr;
                    m_frameToUpload = newFrameNr;
                    uploadNewFrame = true;
                    LOG << "up frame " <<  m_frameToUpload;
                    if (m_frameToUpload == m_decFramePtr)
                        LOGE << " read write same frame";

                    if (!m_framePtr[m_frameToUpload]->width){
                        LOGE << "trying to upload invalid frame!!!";
                        uploadNewFrame = false;
                        m_nrBufferedFrames--;

                    }
                }*/
            }
        }

        if (m_frameToUpload > -1
            && m_consumeFrames
            && uploadNewFrame
            && m_framePtr[m_frameToUpload]->width
            && m_framePtr[m_frameToUpload]->height) {
            m_mutex.lock();

            if (m_decodeYuv420OnGpu) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                if ((AV_PIX_FMT_NV12 == m_srcPixFmt || AV_PIX_FMT_NV21 == m_srcPixFmt) && !m_framePtr.empty()) {
                    // UV interleaved
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, m_textures[1]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth / 2, m_srcHeight / 2,
                                    GL_RG, GL_UNSIGNED_BYTE, m_framePtr[m_frameToUpload]->data[1]);

                    // Y
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth, m_srcHeight,
                                    texture_pixel_format(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[0]);
                }

                // if the video is encoded as YUV420 it's in three separate areas in
                // memory (planar-- not interlaced). so if we have three areas of
                // data, set up one texture unit for each one. in the if statement
                // we'll set up the texture units for chrominance (U & V) and we'll
                // put the luminance (Y) data in GL_TEXTURE0 after the if.

                if (AV_PIX_FMT_YUV420P == m_srcPixFmt && !m_framePtr.empty() && !m_textures.empty()) {
                    // luminance values, whole picture
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_srcWidth, m_srcHeight,
                                    texture_pixel_format(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[0]);

                    int chroma_width = m_srcWidth / 2;
                    int chroma_height = m_srcHeight / 2;

                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, m_textures[1]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                                    texture_pixel_format(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[1]);


                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, m_textures[2]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                                    texture_pixel_format(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[2]);
                }
            } else {
                if (m_usePbos) {
                    m_pboIndex = (m_pboIndex + 1) % m_nrPboBufs;
                    uint nextIndex = (m_pboIndex + 1) % m_nrPboBufs;

                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId());
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbos[m_pboIndex]);

                    glTexSubImage2D(GL_TEXTURE_2D,      // target
                                    0,                      // First mipmap level
                                    0, 0,                   // x and y offset
                                    m_destWidth,              // width and height
                                    m_destHeight,
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
                    glBufferData(GL_PIXEL_UNPACK_BUFFER, m_destWidth * m_destHeight * 4, nullptr, GL_STREAM_DRAW);

                    // map the buffer object into client's memory
                    auto ptr = (GLubyte *) glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, m_destWidth * m_destHeight * 4, GL_MAP_WRITE_BIT);

                    if (ptr && !m_framePtr.empty())
                    {
                        // update data directly on the mapped buffer
                        memcpy(ptr, m_bgraFrame[m_frameToUpload]->data[0], m_destWidth * m_destHeight * 4);
                        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release the mapped buffer
                    }

                    // it is good idea to release PBOs with ID 0 after use.
                    // Once bound with 0, all pixel operations are back to normal ways.
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

                } else {
                    if (!m_framePtr.empty()) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId());
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_destWidth, m_destHeight,
                                        GL_BGR, GL_UNSIGNED_BYTE, m_bgraFrame[m_frameToUpload]->data[0]);
                    }
                }

                if (m_downFrameCb) {
                    m_downFrameCb(m_bgraFrame[m_frameToUpload]->data[0]);
                }
            }

            // mark as consumed
            m_framePtr[m_frameToUpload]->pts = -1;

            if (m_nrBufferedFrames > 0) {
                --m_nrBufferedFrames;
                //LOG <<  "-- " << m_nrBufferedFrames;
            } else {
                //LOGE << " not enough frames buffered";
            }

            m_mutex.unlock();
            m_decodeCond.notify();     // wait until the packet was needed
            m_lastToGlTime = time;
        }
    }
}

GLenum FFMpegDecode::texture_pixel_format(AVPixelFormat srcFmt) {
    std::unordered_map<AVPixelFormat, int> formatMap {
        {AV_PIX_FMT_YUV420P, GL_RED},
        {AV_PIX_FMT_NV12, GL_RED},
        {AV_PIX_FMT_NV21, GL_RED},
        {AV_PIX_FMT_RGB24, GL_BGR},
    };
    return formatMap[srcFmt];
}
#endif

void FFMpegDecode::seek_frame(int64_t _frame_number, double time) {
     // Seek to the keyframe at timestamp.
     // 'timestamp' in 'stream_index'.
     //
     // @param s media file handle
     // @param stream_index If stream_index is (-1), a default
     // stream is selected, and timestamp is automatically converted
     // from AV_TIME_BASE units to the stream specific time_base.
     // @param timestamp Timestamp in AVStream.time_base units
     //        or, if no stream is specified, in AV_TIME_BASE units.
     // @param flags flags which select direction and seeking mode
     // @return >= 0 on success

    // dauert manchmal lange... geht wohl nicht besser
    m_startTime = time;

    av_seek_frame(m_formatContext, m_video_stream_index, _frame_number, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_video_codec_ctx);
}

double FFMpegDecode::get_duration_sec() {
    double sec = static_cast<double>(m_formatContext->duration) / static_cast<double>(AV_TIME_BASE);

    if (sec < m_eps_zero) {
        sec = static_cast<double>(m_formatContext->streams[m_video_stream_index]->duration)
              * r2d(m_formatContext->streams[m_video_stream_index]->time_base);
    }

    if (sec < m_eps_zero) {
        sec = static_cast<double>(m_formatContext->streams[m_video_stream_index]->duration)
              * r2d(m_formatContext->streams[m_video_stream_index]->time_base);
    }

    return sec;
}

int64_t FFMpegDecode::get_total_frames() {
    if (!m_formatContext) {
        return 0;
    }

    if (static_cast<int32_t>(m_formatContext->nb_streams) > m_video_stream_index
        && m_video_stream_index >= 0
        && m_formatContext->streams[m_video_stream_index]) {
        auto nbf = m_formatContext->streams[m_video_stream_index]->nb_frames;
        if (nbf == 0) {
            nbf = std::lround(get_duration_sec() * get_fps());
        }
        return nbf;
    } else {
        return 0;
    }
}

double FFMpegDecode::get_fps() {
    double fps = r2d(m_formatContext->streams[m_video_stream_index]->avg_frame_rate);
    if (fps < m_eps_zero) {
        fps = r2d(m_formatContext->streams[m_video_stream_index]->avg_frame_rate);
    }
    return fps;
}

void FFMpegDecode::dumpEncoders() {
    const AVCodec *current_codec = nullptr;
    void *i{};
    while ((current_codec = av_codec_iterate(&i))) {
        if (av_codec_is_encoder(current_codec)) {
            LOG <<  current_codec->name << " " << current_codec->long_name;
        }
    }
}

void FFMpegDecode::dumpDecoders() {
    const AVCodec *current_codec = nullptr;
    void *i{};
    while ((current_codec = av_codec_iterate(&i))) {
        if (av_codec_is_decoder(current_codec)) {
            LOG <<  current_codec->name << " " << (current_codec->long_name ? current_codec->long_name : "");
        }
    }
}

void FFMpegDecode::forceAudioCodec(const std::string& str) {
    if (str == "AAC") {
        m_forceAudioCodec = AV_CODEC_ID_AAC;
    } else if (str == "MP3") {
        m_forceAudioCodec = AV_CODEC_ID_MP3;
    }
}

void FFMpegDecode::clearResources() {
    m_resourcesAllocated = false;
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
    }

    if (m_video_codec_ctx) {
        avcodec_free_context(&m_video_codec_ctx);
        m_video_codec_ctx = nullptr;
    }

    if (m_audio_codec_ctx) {
        if (m_dst_sampleBuffer) {
            av_freep(&m_dst_sampleBuffer[0]);
        }
        avcodec_free_context(&m_audio_codec_ctx);
        m_audio_codec_ctx = nullptr;
    }

    if (m_audio_swr_ctx) {
        swr_free(&m_audio_swr_ctx);
        m_audio_swr_ctx = nullptr;
    }

    for (auto &it : m_framePtr) {
        av_frame_free(&it);
    }

    m_framePtr.clear();

    if (m_frame) {
        av_frame_unref(m_frame);
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    if (m_audioFrame) {
        av_frame_unref(m_audioFrame);
        av_frame_free(&m_audioFrame);
        m_audioFrame = nullptr;
    }

    if (!m_decodeYuv420OnGpu) {
        m_buffer.clear();
        for (auto &it : m_bgraFrame) {
            av_frame_free(&it);
        }
        m_bgraFrame.clear();
    }

    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

#ifdef ARA_USE_GLBASE
    if (m_usePbos){
        glDeleteBuffers(m_nrPboBufs, &m_pbos[0]);
        m_pbos.clear();
    }
#endif

    m_ptss.clear();

    if (m_hw_device_ctx){
        av_buffer_unref(&m_hw_device_ctx);
        m_hw_device_ctx = nullptr;
    }

    if (m_img_convert_ctx){
        sws_freeContext(m_img_convert_ctx);
        m_img_convert_ctx = nullptr;
    }

#ifdef __ANDROID__
    if(m_mediaCodec) {
        AMediaCodec_stop(m_mediaCodec);
        AMediaCodec_delete(m_mediaCodec);
        m_mediaCodec = nullptr;
    }

    if(m_mediaExtractor) {
        AMediaExtractor_delete(m_mediaExtractor);
        m_mediaExtractor = nullptr;
    }

    if(m_bsfCtx) {
        av_bsf_free(&m_bsfCtx);
        m_bsfCtx = nullptr;
    }
#endif

}

}

#endif