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

void FFMpegDecode::openFile(const ffmpeg::DecodePar& p) {
    m_par = p;
    initFFMpeg();
    try {
        if (p.useHwAccel) {
            setupHwDecoding();
        }
        allocFormatContext();
        checkForNetworkSrc(m_par);

        if (p.doStart) {
            m_decodeThread = std::thread([this]{
                m_startTime = 0.0;
                m_run = true;
                setupStreams(nullptr, &m_formatOpts, m_par);
                allocateResources(m_par);
                singleThreadDecodeLoop();
            });
            m_decodeThread.detach();
        } else {
            setupStreams(nullptr, &m_formatOpts, m_par);
        }
    } catch (std::runtime_error& e) {
        LOGE << "FFmpeg::openFile Error: " << e.what();
    }
}

void FFMpegDecode::openCamera(const ffmpeg::DecodePar& p) {
    m_par = p;
#ifdef _WIN32
    m_par.filePath = "video="+m_par.filePath;
#endif
    m_par.useNrThreads = 2;
    m_hasNoTimeStamp = true;
    m_isStream = true;
    m_videoFrameBufferSize = 2;

    initFFMpeg();

    try {
        allocFormatContext();
#ifdef _WIN32
        auto camInputFormat = av_find_input_format("dshow");
#elif __linux__
        auto camInputFormat = av_find_input_format("v4l2");
#elif __APPLE__
        auto camInputFormat = (AVInputFormat*)av_find_input_format("avfoundation");
#endif
        if (!camInputFormat) {
            throw runtime_error("couldn't find input m_format dshow");
        }

        setupStreams(camInputFormat, &m_formatOpts, m_par);
    } catch (std::runtime_error& e) {
        LOGE << "FFmpeg::openFile Error: " << e.what();
    }
}

void FFMpegDecode::initFFMpeg() {
    avdevice_register_all();
    avformat_network_init();
    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback(&ffmpeg::LogCallbackShim);    // custom logging
}

void FFMpegDecode::setupHwDecoding() {
    setDefaultHwDevice();
    m_hwDeviceType = av_hwdevice_find_type_by_name(m_defaultHwDevType.c_str());
    checkHwDeviceType();
    m_hwPixFmt = findFmtByHwType(m_hwDeviceType);
    if (m_hwPixFmt == -1) {
        throw runtime_error("Hardware acceleration "+m_defaultHwDevType+" not support");
    }
}

void FFMpegDecode::checkHwDeviceType() {
    if (m_hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
        LOGE << "Available device types:";
        while((m_hwDeviceType = av_hwdevice_iterate_types(m_hwDeviceType)) != AV_HWDEVICE_TYPE_NONE) {
            LOG <<  av_hwdevice_get_type_name(m_hwDeviceType);
        }
        throw runtime_error("Device type "+m_defaultHwDevType+" is not supported");
    }
    LOG << "FFMpegDecode: found hwDeviceType " << av_hwdevice_get_type_name(m_hwDeviceType);
}

void FFMpegDecode::allocFormatContext() {
    // AVFormatContext holds the header information from the m_format (Container)
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        throw runtime_error("ERROR could not allocate memory for Format Context");
    }

    if (!av_dict_get(m_formatOpts, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE)) {
        av_dict_set(&m_formatOpts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        m_scanAllPmtsSet = 1;
    }
    av_dict_parse_string(&m_formatOpts, "", ":", ",", 0);
}

void FFMpegDecode::checkForNetworkSrc(const ffmpeg::DecodePar& p) {
    if (p.filePath.substr(0, 6) == "mms://" || p.filePath.substr(0, 7) == "mmsh://" ||
        p.filePath.substr(0, 7) == "mmst://" || p.filePath.substr(0, 7) == "mmsu://" ||
        p.filePath.substr(0, 7) == "http://" || p.filePath.substr(0, 8) == "https://" ||
        p.filePath.substr(0, 7) == "rtmp://" || p.filePath.substr(0, 6) == "udp://" ||
        p.filePath.substr(0, 7) == "rtsp://" || p.filePath.substr(0, 6) == "rtp://" ||
        p.filePath.substr(0, 6) == "ftp://" || p.filePath.substr(0, 7) == "sftp://" ||
        p.filePath.substr(0, 6) == "tcp://" || p.filePath.substr(0, 7) == "unix://" ||
        p.filePath.substr(0, 6) == "smb://") {
        m_isStream = true;
        m_scanAllPmtsSet = 1;
        m_formatContext->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

        if (p.filePath.substr(0, 7) == "rtmp://") {
            av_dict_set(&m_formatOpts, "probesize", "64", 0); // with 64 sometimes errors (full hd + audio wrong samplerate)
            av_dict_set(&m_formatOpts, "analyzeduration", "1", 0);
            m_hasNoTimeStamp = true;
            m_videoFrameBufferSize=24;
            m_nrFramesToStart=8;

        } else if (p.filePath.substr(0, 7) == "http://") {
            m_hasNoTimeStamp = true;
        }
    }
}

void FFMpegDecode::setDefaultHwDevice() {
#if defined(__linux__) && !defined(ANDROID)
    m_defaultHwDevType = "vdpau";
#elif _WIN32
    m_defaultHwDevType = "d3d11va";
#elif __APPLE__
    m_defaultHwDevType = "videotoolbox";
#endif
}

bool FFMpegDecode::setupStreams(const AVInputFormat* format, AVDictionary** options, ffmpeg::DecodePar& p) {
    int err, ret;
    if ((err = avformat_open_input(&m_formatContext, !p.filePath.empty() ? p.filePath.c_str() : nullptr, format, options) != 0)) {
        throw runtime_error("ERROR could not open the file "+p.filePath+" "+err2str(err));
    }
    if (m_scanAllPmtsSet) {
        av_dict_set(&m_formatOpts, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE);
    }

    AVDictionaryEntry *t{};
    if ((t = av_dict_get(m_formatOpts, "", nullptr, AV_DICT_IGNORE_SUFFIX))) {
        throw runtime_error("Option "+std::string(t->key)+" not found.");
    }
    if (m_genpts) {
        m_formatContext->flags |= AVFMT_FLAG_GENPTS;
    }

    av_format_inject_global_side_data(m_formatContext);
    initStreamInfo();

    if (m_formatContext->pb) {
        m_formatContext->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    }
    if (m_seekByBytes < 0) {
        m_seekByBytes = !!(m_formatContext->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", m_formatContext->iformat->name) != 0;
    }

    parseSeeking();

    if (!p.filePath.empty()) {
        av_dump_format(m_formatContext, 0, p.filePath.c_str(), 0);
    }

    // the component that knows how to enCOde and DECode the stream, it's the codec (audio or video) http://ffmpeg.org/doxygen/trunk/structAVCodec.html
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;

    // loop though all the streams and print its main information
    for (auto i = 0; i < m_formatContext->nb_streams; ++i) {
        auto localCodecParameters = m_formatContext->streams[i]->codecpar;
        if (!localCodecParameters) {
            continue;
        }
        if (localCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO && m_forceAudioCodec) {
            localCodecParameters->codec_id = m_forceAudioCodec;
        }

        // finds the registered decoder for a codec ID
        auto localCodec = getCodecFromId(localCodecParameters->codec_id);

        // when the stream is a video we store its index, codec parameters and codec
        if (localCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            parseVideoCodecPar(i, localCodecParameters, localCodec);
        } else if (localCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            parseAudioCodecPar(i, localCodecParameters, localCodec);
        }
    }

    if (p.initCb) {
        p.initCb();
    }

    return true;
}

void FFMpegDecode::parseVideoCodecPar(int32_t i, AVCodecParameters* localCodecParameters, const AVCodec* ) {
    m_videoStreamIndex = i;
    ++m_videoNrTracks;
    m_videoCodecCtx = avcodec_alloc_context3(nullptr);
    if (!m_videoCodecCtx) {
        throw runtime_error("failed to allocated memory for video  AVCodecContext");
    }

    // set the number of threads here
    if (!m_par.useHwAccel) {
        m_videoCodecCtx->thread_count = m_par.useNrThreads;
    }

    // Fill the codec context based on the values from the supplied codec parameters
    if (avcodec_parameters_to_context(m_videoCodecCtx, localCodecParameters) < 0) {
        throw runtime_error("failed to copy codec params to video codec context");
    }

    auto video_codec = avcodec_find_decoder(m_videoCodecCtx->codec_id);

    // optionally forcing a specific codec type
    if (m_videoCodecName) {
        video_codec = avcodec_find_decoder_by_name(m_videoCodecName);
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = avcodec_alloc_context3(video_codec);
    }

    if (!video_codec) {
        throw m_videoCodecName ? runtime_error("No codec could be found with name "+std::string(m_videoCodecName))
                               : runtime_error("No decoder could be found for codec "+std::string(avcodec_get_name(m_videoCodecCtx->codec_id)));
    }

    m_videoCodecCtx->pkt_timebase = m_formatContext->streams[i]->time_base;
    m_videoCodecCtx->codec_id = video_codec->id;

    if (m_par.useHwAccel) {
        m_videoCodecCtx->get_format = ffmpeg::getHwFormat;
        av_opt_set_int(m_videoCodecCtx, "refcounted_frames", 1, 0);    // what does this do?

        ffmpeg::hwFormatToCheck = m_hwPixFmt;
        if (initHwDecode(m_videoCodecCtx, m_hwDeviceType) < 0) {
            throw runtime_error("initHwDecode failed");
        }

        m_hwPixFmt = ffmpeg::hwFormatToCheck;
    }

    // save basic codec parameters for access from outside
    m_srcPixFmt     = m_videoCodecCtx->pix_fmt;
    m_srcWidth      = m_videoCodecCtx->width;
    m_srcHeight     = m_videoCodecCtx->height;
    m_bitCount      = m_videoCodecCtx->bits_per_raw_sample;
    m_timeBaseDiv   = static_cast<double>(m_formatContext->streams[i]->time_base.num) /
                      static_cast<double>(m_formatContext->streams[i]->time_base.den);

    if (m_formatContext->streams[i]->r_frame_rate.num) {
        m_frameDur = static_cast<double>(m_formatContext->streams[i]->r_frame_rate.den) /
                     static_cast<double>(m_formatContext->streams[i]->r_frame_rate.num);
    } else {
        m_frameDur = 0.0;
    }

    m_fps = static_cast<int32_t>((static_cast<double>(m_formatContext->streams[i]->r_frame_rate.num) /
                                  static_cast<double>(m_formatContext->streams[i]->r_frame_rate.den)));

    if (m_par.decodeYuv420OnGpu || !m_par.destWidth) {
        m_par.destWidth = m_srcWidth;
    }

    if (m_par.decodeYuv420OnGpu || !m_par.destHeight) {
        m_par.destHeight = m_srcHeight;
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    int ret=0;
    if ((ret = avcodec_open2(m_videoCodecCtx, video_codec, nullptr)) < 0) {
        throw runtime_error("failed to open video codec through avcodec_open2 "+av_make_error_string(ret));
    }
}

void FFMpegDecode::parseAudioCodecPar(int32_t i, AVCodecParameters* localCodecParameters, const AVCodec* localCodec) {
    // TODO : there is a problem with 5.1, AAC which is detected as 1 channel
    m_audioNumChannels = localCodecParameters->channels;
    m_audioStreamIndex = i;
    m_audioCodec = localCodec;

    // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
    m_audioCodecCtx = avcodec_alloc_context3(localCodec);
    if (!m_audioCodecCtx) {
        throw runtime_error("failed to allocated memory for audio  AVCodecContext");
    }

    // Fill the codec context based on the values from the supplied codec parameters
    if (avcodec_parameters_to_context(m_audioCodecCtx, localCodecParameters) < 0) {
        throw runtime_error("failed to copy codec params to audio codec context");
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    if (avcodec_open2(m_audioCodecCtx, m_audioCodec, nullptr) < 0) {
        throw runtime_error("failed to open audio codec through avcodec_open2");
    }
}

void FFMpegDecode::initStreamInfo() {
    // read Packets from the Format to get stream information
    // this function populates m_formatContext->streams (of size equals to m_formatContext->nb_streams)
    // the arguments are: the AVFormatContext and options contains options for codec corresponding to i-th stream.
    // On return each dictionary will be filled with options that were not found.
    auto opts = setupFindStreamInfoOpts(m_formatContext, m_codecOpts);
    auto origNumStreams = static_cast<int32_t>(m_formatContext->nb_streams);
    auto err = avformat_find_stream_info(m_formatContext, opts);
    for (auto i = 0; i < origNumStreams; ++i) {
        av_dict_free(&opts[i]);
    }
    av_freep(&opts);
    if (err < 0) {
        throw runtime_error(m_par.filePath+" could not find codec parameters");
    }
}

void FFMpegDecode::parseSeeking() {
    if (!m_isStream && m_start_time != AV_NOPTS_VALUE) {
        int64_t timestamp = m_start_time;
        // add the stream start time
        if (m_formatContext->start_time != AV_NOPTS_VALUE) {
            timestamp += m_formatContext->start_time;
        }
        if (avformat_seek_file(m_formatContext, -1, INT64_MIN, timestamp, INT64_MAX, 0) < 0) {
            LOG << m_par.filePath <<  ": could not seek to position " <<  static_cast<double>(timestamp) / AV_TIME_BASE;
        }
    }
}

void FFMpegDecode::allocateResources(ffmpeg::DecodePar& p) {
    m_packet = av_packet_alloc();
    if (!m_packet) {
        throw runtime_error("failed to allocated memory for AVPacket");
    }

    if (p.destWidth && p.destHeight) {
        m_framePtr = std::vector<AVFrame*>(m_videoFrameBufferSize);
        for (auto &it : m_framePtr) {
            it = av_frame_alloc();
            it->width = p.destWidth;
            it->height = p.destHeight;
            it->pts = -1;
            if (!it) {
                throw runtime_error("failed to allocated memory for AVFrame");
            }
        }
    }

    m_frame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();

    m_ptss = std::vector<double>(m_videoFrameBufferSize);
    std::fill(m_ptss.begin(), m_ptss.end(), -1.0);

    m_totNumFrames = static_cast<uint>(getTotalFrames());
    m_resourcesAllocated = true;
}

void FFMpegDecode::start(double time) {
    allocateResources(m_par);
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

AVFrame *FFMpegDecode::allocPicture(enum AVPixelFormat pix_fmt, int width, int height,
                                    std::vector<std::vector<uint8_t>>::iterator buf) {
    auto picture = av_frame_alloc();
    if (!picture) {
        return nullptr;
    }

    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;
    picture->pts = -1;

    // Allocate memory for the raw data we get when converting.
    *buf = vector<uint8_t>( av_image_get_buffer_size(pix_fmt, width, height, 1) );

    // Assign appropriate parts of buffer to image planes in m_inpFrame
    av_image_fill_arrays(picture->data, picture->linesize, &(*buf)[0], pix_fmt, width, height, 1);
    return picture;
}

bool FFMpegDecode::setAudioConverter(int destSampleRate, AVSampleFormat format) {
    m_useAudioConversion = true;
    if (!m_audioCodecCtx){
        LOGE << "FFMpegDecode::setAudioConverter failed!, m_audioCodecCtx == NULL";
        return false;
    }
    m_dstChannelLayout = m_audioCodecCtx->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    m_dstSampleRate = destSampleRate;
    m_dstAudioNumChannels = m_dstChannelLayout == AV_CH_LAYOUT_MONO ? 1 : 2;
    m_dstSampleFmt = format;

    // create resampler context
    m_audioSwrCtx = swr_alloc();
    if (!m_audioSwrCtx) {
        LOGE << "Could not allocate resampler context";
        //ret = AVERROR(ENOMEM);
        return false;
    }

    // If you don't know the channel layout, get it from the number of channels.
    if (m_audioCodecCtx->channel_layout == 0) {
        m_audioCodecCtx->channel_layout = av_get_default_channel_layout(m_audioCodecCtx->channels);
    }

    // set options
    av_opt_set_int(m_audioSwrCtx, "in_channel_layout", static_cast<int64_t>(m_audioCodecCtx->channel_layout), 0);
    av_opt_set_int(m_audioSwrCtx, "in_sample_rate", m_audioCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(m_audioSwrCtx, "in_sample_fmt", m_audioCodecCtx->sample_fmt, 0);

    av_opt_set_int(m_audioSwrCtx, "out_channel_layout", m_dstChannelLayout, 0);
    av_opt_set_int(m_audioSwrCtx, "out_sample_rate", m_dstSampleRate, 0);
    av_opt_set_sample_fmt(m_audioSwrCtx, "out_sample_fmt", m_dstSampleFmt, 0);

    // initialize the resampling context
    if (swr_init(m_audioSwrCtx) < 0) {
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
            if ((m_videoStreamIndex > -1
                 && static_cast<int32_t>(m_nrBufferedFrames) >= static_cast<int32_t>(m_videoFrameBufferSize))) {
                this_thread::sleep_for(500us);
                continue;
            }

            if (av_read_frame(m_formatContext, m_packet) < 0) {
                continue;
            }

            // if it's the video stream and the m_buffer queue is not filled
            if (m_packet->stream_index == m_videoStreamIndex) {
                // we are using multiple frames, so the frames reaching here are not
                // in a continuous order!!!!!!
                m_actFrameNr = static_cast<uint32_t>(static_cast<double>(m_packet->pts) * m_timeBaseDiv / m_frameDur);

                if ((m_totNumFrames - 1) == m_actFrameNr && m_loop && !m_isStream) {
                    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                }

                if (decodeVideoPacket(m_packet, m_videoCodecCtx) < 0) {
                    continue;
                }
            } else if (m_packet->stream_index == m_audioStreamIndex
                       && decodeAudioPacket(m_packet, m_audioCodecCtx) < 0) {
                continue;
            }

            av_packet_unref(m_packet);
        } else {
            this_thread::sleep_for(1000us);
        }
    }

    m_endThreadCond.notify();	 // wait until the packet was needed
}

int32_t FFMpegDecode::decodeVideoPacket(AVPacket* packet, AVCodecContext* codecContext) {
    auto response = sendPacket(packet, codecContext);
    while (m_run && response >= 0) {
        if (m_run) {
            response = checkReceiveFrame(codecContext);

            if (response == AVERROR(EAGAIN)) {
                break;
            } else if (response == AVERROR_EOF) {
                LOGE <<  "end of file";
            } else if (response < 0) {
                return response;
            }

            if (m_run && response >= 0) {
                response = parseReceivedFrame(codecContext);
            }
        }
    }
    return 0;
}

int32_t FFMpegDecode::sendPacket(AVPacket* packet, AVCodecContext* codecContext) {
    return avcodec_send_packet(codecContext, packet);
}

int32_t FFMpegDecode::checkReceiveFrame(AVCodecContext* codecContext) {
    int32_t response = 0;
    if (m_par.useHwAccel) {
        response = avcodec_receive_frame(codecContext, m_frame);            // always calls av_frame_unref
    } else {
        m_mutex.lock();
        response = avcodec_receive_frame(codecContext, m_framePtr[m_decFramePtr]);    // always calls av_frame_unref
        m_mutex.unlock();
    }
    return response;
}

int32_t FFMpegDecode::parseReceivedFrame(AVCodecContext* codecContext) {
    m_mutex.lock();

    // convert frame to desired size and m_format
    if (m_par.useHwAccel && m_frame->format == m_hwPixFmt) {
        transferFromHwToCpu();
        m_srcPixFmt = static_cast<AVPixelFormat>(m_framePtr[m_decFramePtr]->format);
    }

    if (!m_par.decodeYuv420OnGpu) {
        // since now for the first time we are really sure about the pix_fmt the decode
        // frame will have, initialize the textures and the swscale context if necessary
        if (convertFrameToCpuFormat(codecContext) < 0) {
            LOGE << "FFMpegDecode ERROR, sws_scale failed!!!";
        }

        if (m_decodeCb) {
            m_decodeCb(m_bgraFrame[m_decFramePtr]->data[0]);
        }
    } else if (!m_par.useHwAccel) {
        m_srcPixFmt = (AVPixelFormat) codecContext->pix_fmt;
    }

    m_mutex.unlock();

    if (m_videoCb) {
        m_videoCb(m_framePtr[m_decFramePtr]);
    }

    m_mutex.lock();

    m_ptss[m_decFramePtr] = m_timeBaseDiv * (double) m_framePtr[m_decFramePtr]->pts;

    // the stream might start with a pts different from 0, for this reason here register explicitly the starting pts
    if (m_nrBufferedFrames == 0) {
        m_videoStartPts = m_par.useHwAccel ? m_frame->pts : m_framePtr[m_decFramePtr]->pts;
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

    m_decFramePtr = ++m_decFramePtr % m_videoFrameBufferSize;
    m_mutex.unlock();
    return -1; // break loop
}

void FFMpegDecode::transferFromHwToCpu() {
    // retrieve data from GPU to CPU, dst m_frame must be "clean"
    if (av_hwframe_transfer_data(m_framePtr[m_decFramePtr], m_frame, 0) < 0) {
        LOGE << "Error transferring the data to system memory";
    }

    m_framePtr[m_decFramePtr]->pts = m_frame->pts;
    m_framePtr[m_decFramePtr]->pkt_size = m_frame->pkt_size;
    m_framePtr[m_decFramePtr]->coded_picture_number = m_frame->coded_picture_number;
    m_framePtr[m_decFramePtr]->pict_type = m_frame->pict_type;
}

int32_t FFMpegDecode::convertFrameToCpuFormat(AVCodecContext* codecContext) {
    if (!m_imgConvertCtx) {
        m_imgConvertCtx = sws_getCachedContext(m_imgConvertCtx,
                                               codecContext->width, codecContext->height,
                                               (AVPixelFormat) m_framePtr[m_decFramePtr]->format,
                                               m_par.destWidth, m_par.destHeight, m_destPixFmt,
                                               SWS_FAST_BILINEAR, //SWS_BICUBIC,
                                                 nullptr, nullptr, nullptr);
    }

    return sws_scale(m_imgConvertCtx,
                     m_framePtr[m_decFramePtr]->data, m_framePtr[m_decFramePtr]->linesize, 0,
                     codecContext->height,
                     m_bgraFrame[m_decFramePtr]->data, m_bgraFrame[m_decFramePtr]->linesize);
}

int FFMpegDecode::decodeAudioPacket(AVPacket *packet, AVCodecContext *codecContext) {
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
            if (m_audioCodecCtx->codec_id == AV_CODEC_ID_MP3 && data_size < 4096) {
                continue;
            }

            if (m_useAudioConversion) {
                // init the destination buffer if necessary
                if (!m_dstSampleBuffer) {
                    m_maxDstNumSamples = m_dstNumSamples = (int) av_rescale_rnd(m_audioFrame->nb_samples, m_dstSampleRate,
                                                                                m_audioCodecCtx->sample_rate, AV_ROUND_UP);

                    // buffer is going to be directly written to a rawaudio file, no alignment
                    m_dstAudioNumChannels = av_get_channel_layout_nb_channels(m_dstChannelLayout);
                    response = av_samples_alloc_array_and_samples((uint8_t***)&m_dstSampleBuffer, &m_dstAudioLineSize,
                                                                  m_dstAudioNumChannels, m_dstNumSamples, m_dstSampleFmt, 0);

                    if (response < 0) {
                        LOGE << "ERROR: could not allocate destination sample buffer";
                        break;
                    }
                }

                // convert to destination m_format
                response = swr_convert(m_audioSwrCtx, m_dstSampleBuffer, m_dstNumSamples,
                                       (const uint8_t**)m_audioFrame->data, m_audioFrame->nb_samples);

                if (response < 0) {
                    LOGE << "Error while converting";
                    break;
                }

                m_audioCbData.nChannels = m_dstAudioNumChannels;
                m_audioCbData.samples = m_dstNumSamples;
                m_audioCbData.byteSize = m_dstAudioLineSize;
                m_audioCbData.buffer = m_dstSampleBuffer;
                m_audioCbData.sampleRate = m_dstSampleRate;
                m_audioCbData.sampleFmt = m_dstSampleFmt;

                if (m_audioCb) {
                    m_audioCb(m_audioCbData);
                }
            } else {
                if (!m_dstSampleBuffer) {
                    // buffer is going to be directly written to a raw audio, no alignment
                    response = av_samples_alloc_array_and_samples((uint8_t***)&m_dstSampleBuffer, &m_audioFrame->linesize[0],
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

void FFMpegDecode::seekFrame(int64_t frame_number, double time) {
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

    av_seek_frame(m_formatContext, m_videoStreamIndex, frame_number, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_videoCodecCtx);
}

int FFMpegDecode::initHwDecode(AVCodecContext *ctx, const enum AVHWDeviceType type) {
    int err = 0;
    if ((err = av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0)) < 0) {
        LOGE <<  "Failed to create specified HW device.";
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    return err;
}

double FFMpegDecode::getDurationSec() {
    double sec = static_cast<double>(m_formatContext->duration) / static_cast<double>(AV_TIME_BASE);

    if (sec < m_epsZero) {
        sec = static_cast<double>(m_formatContext->streams[m_videoStreamIndex]->duration)
              * r2d(m_formatContext->streams[m_videoStreamIndex]->time_base);
    }

    if (sec < m_epsZero) {
        sec = static_cast<double>(m_formatContext->streams[m_videoStreamIndex]->duration)
              * r2d(m_formatContext->streams[m_videoStreamIndex]->time_base);
    }

    return sec;
}

int64_t FFMpegDecode::getTotalFrames() {
    if (!m_formatContext) {
        return 0;
    }

    if (static_cast<int32_t>(m_formatContext->nb_streams) > m_videoStreamIndex
        && m_videoStreamIndex >= 0
        && m_formatContext->streams[m_videoStreamIndex]) {
        auto nbf = m_formatContext->streams[m_videoStreamIndex]->nb_frames;
        if (nbf == 0) {
            nbf = std::lround(getDurationSec() * getFps());
        }
        return nbf;
    } else {
        return 0;
    }
}

double FFMpegDecode::getFps() {
    double fps = r2d(m_formatContext->streams[m_videoStreamIndex]->avg_frame_rate);
    if (fps < m_epsZero) {
        fps = r2d(m_formatContext->streams[m_videoStreamIndex]->avg_frame_rate);
    }
    return fps;
}

void FFMpegDecode::clearResources() {
    m_resourcesAllocated = false;
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
    }

    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }

    if (m_audioCodecCtx) {
        if (m_dstSampleBuffer) {
            av_freep(&m_dstSampleBuffer[0]);
        }
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }

    if (m_audioSwrCtx) {
        swr_free(&m_audioSwrCtx);
        m_audioSwrCtx = nullptr;
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

    if (!m_par.decodeYuv420OnGpu) {
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

    m_ptss.clear();

    if (m_hwDeviceCtx){
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    if (m_imgConvertCtx){
        sws_freeContext(m_imgConvertCtx);
        m_imgConvertCtx = nullptr;
    }
}

}

#endif