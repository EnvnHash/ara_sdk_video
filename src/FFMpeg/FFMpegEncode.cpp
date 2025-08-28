/*
 * FFMpegEncode.cpp
 *
 *  Created on: 29.06.2016
 *      Copyright by Sven Hahne
 *
 *      bei .avi stimmt wird die gesamtlaenge nicht richtig geschrieben...
 */

#ifdef ARA_USE_FFMPEG

#include "FFMpeg/FFMpegEncode.h"
#include "FFMpeg/FFMpegDecode.h"

using namespace std;
using namespace ara::av::ffmpeg;

namespace ara::av {

FFMpegEncode::FFMpegEncode(const EncodePar &par) {
    init(par);
}

bool FFMpegEncode::init(const EncodePar &par) {
    m_par = par;
    if (m_par.fromDecoder) {
        m_par.fps = static_cast<int32_t>(m_par.fromDecoder->getFps(streamType::video));
        m_par.width = static_cast<int32_t>(m_par.fromDecoder->getPar().destWidth);
        m_par.height = static_cast<int32_t>(m_par.fromDecoder->getPar().destHeight);
    }
    m_frameDur = 1000.0 / m_par.fps; // in ms

    initFFMpeg();

    m_glDownloadFmt = getGlColorFormatFromAVPixelFormat(par.pixelFormat);
    m_glNrBytesPerPixel = getNumBytesPerPix(m_glDownloadFmt);
    m_nbytes = m_par.width * m_par.height * m_glNrBytesPerPixel;

    // create buffers for downloading images from opengl
    m_videoFrames.allocate(m_nrBufferFrames);
    for (auto &it: m_videoFrames.getBuffer()) {
        it.buffer.resize(m_nbytes);
        it.encTime = -1.0;
    }

    m_pbos.allocate(m_num_pbos);
    glGenBuffers(static_cast<GLsizei>(m_num_pbos), m_pbos.getBuffer().data());
    for (auto &it: m_pbos.getBuffer()) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, it);
        glBufferData(GL_PIXEL_PACK_BUFFER, m_nbytes, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_inited = true;
    return true;
}

bool FFMpegEncode::record(double time) {
    for (auto &it: m_outStream) {
        it = {nullptr};
    }

    m_fileType = std::filesystem::path(m_par.filePath).extension().string();
    AVDictionary *avioOpts = nullptr;

    try {
        checkRtmpRequested(avioOpts);
        LOG << "FFMpegEncode recording to " << m_par.filePath;
        if (m_par.useHwAccel) {
            setupHardwareContext();
        }

        allocFormatCtx();
        addStreams();
        openStreams();
        openOutputFile(avioOpts);

#ifdef WITH_AUDIO
        m_mixDownMap = std::vector< std::vector<unsigned int> >(m_outStream[toType(streamType::audio)].st->codecpar->channels);
        unsigned int m_nrMixChans = static_cast<unsigned int>( (float(pa->getMaxNrInChannels()) / float(m_outStream[toType(streamType::audio)].st->codecpar->channels)) + 0.5f);

        // make m_mixDownMap
        for (int chan=0; chan<m_outStream[toType(streamType::audio)].st->codecpar->channels; chan++)
            for (unsigned int i=0;i<m_nrMixChans;i++)
                if ( chan + i * m_nrMixChans < pa->getMaxNrInChannels() )
                    m_mixDownMap[chan].push_back(chan + i * m_nrMixChans);
    */
                // start downloading audio frames
                //pa->setExtCallback(&FFMpegEncode::getMediaRecAudioDataCallback, &m_audioQueue, m_outStream[toType(streamType::audio)].st->codecpar->frame_size,
                //		m_outStream[toType(streamType::audio)].st->codecpar->channels, &m_mixDownMap);

                //pa->setExtCallback(std::bind(&FFMpegEncode::mediaRecAudioDataCallback, this, std::placeholders::_1));

                /*
                    if ( m_outStream[toType(streamType::video)].enc->pix_fmt == PIX_FMT_YUV420P )
                    {
                        // gamma(0.1:1:10), contrast(1), brightness(0), saturation(1), gamma_r(1), gamma_grün(1), gamma_blau(1)
                        // korrektur für rgb -> yuv wird sonst ein wenig matt...
                        const char *filter_descr = "mp=eq2=2.0:2.0:0.0:2.0";

                        if ((m_ret = init_filters(filter_descr, recP)) < 0)
                            LOGE << 'Error occurred initing filters\n");

                        recP->m_useFiltering = true;
                        std::cout << "use filtering " << std::endl;
                    }
                */
#endif

        m_doRec = true;
        m_gotFirstFrame = false;
    } catch (runtime_error& e) {
        LOGE << "FFMpegEncode Error: " << e.what();
    }

    if (m_par.fromDecoder) {
        m_par.fromDecoder->start(time);
    }

    m_Thread = new std::thread(&FFMpegEncode::recThread, this);
    m_Thread->detach();

    return true;
}

void FFMpegEncode::setupHardwareContext() {
    auto forceCodeCopy = m_forceCodec;

#ifdef __linux__
    m_ret = av_hwdevice_ctx_create(&m_hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    m_hwPixFmt = AV_PIX_FMT_CUDA;
    m_hwSwFmt = AV_PIX_FMT_NV12;
    m_forceCodec = "h264_nvenc";

    if (m_ret < 0) {
        m_ret = av_hwdevice_ctx_create(&m_hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        m_hwPixFmt = AV_PIX_FMT_VAAPI;
        m_hwSwFmt = AV_PIX_FMT_NV12;
        m_forceCodec = "h264_vaapi";
    }
#elif __APPLE__
    m_ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
            m_hwPixFmt = AV_PIX_FMT_VIDEOTOOLBOX;
            m_hwSwFmt = AV_PIX_FMT_NV12;
            m_forceCodec = "h264_videotoolbox";
#elif _WIN32
            m_ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
            m_hwPixFmt = AV_PIX_FMT_CUDA;
            m_hwSwFmt = AV_PIX_FMT_NV12;
            m_forceCodec = "h264_nvenc";

            if (m_ret < 0) {
                m_ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
                m_hwPixFmt = AV_PIX_FMT_D3D11VA_VLD;
                m_hwSwFmt = AV_PIX_FMT_NV12;
                m_forceCodec = "h264_d3d11va2";
            }

            if (m_ret < 0) {
                m_ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
                m_hwPixFmt = AV_PIX_FMT_DXVA2_VLD;
                m_hwSwFmt = AV_PIX_FMT_NV12;
                m_forceCodec = "h264_dxva2";
            }
#endif
    if (m_ret < 0) {
        // LOGE << "Failed to create a hw encoding context. using sw fallback Error code: " << ffmpeg::err2str(m_ret);
        m_par.useHwAccel = false;
        m_forceCodec = forceCodeCopy;
    }
}

void FFMpegEncode::checkRtmpRequested(AVDictionary *avioOpts) {
    if (m_par.filePath.substr(0, 6) == "mms://" || m_par.filePath.substr(0, 7) == "mmsh://" ||
        m_par.filePath.substr(0, 7) == "mmst://" || m_par.filePath.substr(0, 7) == "mmsu://" ||
        m_par.filePath.substr(0, 7) == "http://" || m_par.filePath.substr(0, 8) == "https://" ||
        m_par.filePath.substr(0, 7) == "rtmp://" || m_par.filePath.substr(0, 6) == "udp://" ||
        m_par.filePath.substr(0, 7) == "rtsp://" || m_par.filePath.substr(0, 6) == "rtp://" ||
        m_par.filePath.substr(0, 6) == "ftp://" || m_par.filePath.substr(0, 7) == "sftp://" ||
        m_par.filePath.substr(0, 6) == "tcp://" || m_par.filePath.substr(0, 7) == "unix://" ||
        m_par.filePath.substr(0, 6) == "smb://") {
        //av_dict_set(&avioOpts, "rtsp_transport", "udp", 0);
        //av_dict_set(&d, "max_delay", "500000", 0);

        if (m_par.filePath.substr(0, 7) == "rtmp://") {
            m_forceCodec = "libx264";

            if (av_dict_set(&avioOpts, "tcp_nodelay", "1", 0) < 0) // Use TCP_NODELAY to disable Nagle's algorithm
                LOGE << "error tcp_nodelay";

            if (av_dict_set(&avioOpts, "rtmp_buffer", "3000", 0) < 0) // buffer size in millisec, default 3000
                LOGE << "error rtmp_buffer";

            if (av_dict_set(&avioOpts, "rtmp_live", "1", 0) < 0) // Specify that the media is a live stream
                LOGE << "error rtmp_live";

/*
             * // rtmp options
            {"rtmp_conn", "Append arbitrary AMF data to the Connect message", OFFSET(conn), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
            {"any", "both", 0, AV_OPT_TYPE_CONST, {.i64 = -2}, 0, 0, DEC, "rtmp_live"},
            {"recorded", "recorded stream", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, 0, 0, DEC, "rtmp_live"},
            {"rtmp_pageurl", "URL of the web page in which the media was embedded. By default no value will be sent.", OFFSET(pageurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
            {"rtmp_playpath", "Stream identifier to play or to publish", OFFSET(playpath), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
            {"rtmp_subscribe", "Name of live stream to subscribe to. Defaults to rtmp_playpath.", OFFSET(subscribe), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
            {"rtmp_swfhash", "SHA256 hash of the decompressed SWF file (32 bytes).", OFFSET(swfhash), AV_OPT_TYPE_BINARY, .flags = DEC},
            {"rtmp_swfsize", "Size of the decompressed SWF file, required for SWFVerification.", OFFSET(swfsize), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC},
            {"rtmp_swfurl", "URL of the SWF player. By default no value will be sent", OFFSET(swfurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
            {"rtmp_swfverify", "URL to player swf file, compute hash/size automatically.", OFFSET(swfverify), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC},
            {"rtmp_tcurl", "URL of the target stream. Defaults to proto://host[:port]/app.", OFFSET(tcurl), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, DEC|ENC},
            {"rtmp_listen", "Listen for incoming rtmp connections", OFFSET(listen), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
            {"listen",      "Listen for incoming rtmp connections", OFFSET(listen), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
            {"tcp_nodelay", "", OFFSET(tcp_nodelay), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, DEC|ENC},
            {"timeout", "Maximum timeout (in seconds) to wait for incoming connections. -1 is infinite. Implies -rtmp_listen 1",  OFFSET(listen_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC, "rtmp_listen" },
 */

            // rtmp streaming on Windows is a problem. In case of a bad connection and higher bitrates
            // av_interleaved_write_frame seems to wait on each packet until the server responds which
            // makes it unusable (on linux there is no problem with that). Using librtmp instead of the
            // ffmpegs own implementation solves these problems. Since compiling ffmpeg on Windows is a pain
            // we use ara sdks librtmp implementation here
#ifdef ARA_USE_LIBRTMP
            m_rtmpUrl = m_par.filePath;
                //m_rtmpSender.connect(std::string(m_par.filePath), true);
                m_par.filePath = "temp_stream.flv";
#endif
            m_isRtmp = true;
        }

        m_is_net_stream = true;
    }
}

void FFMpegEncode::allocFormatCtx() {
    avformat_alloc_output_context2(&m_oc, nullptr, m_isRtmp ? "flv" : nullptr, m_par.filePath.c_str());
    if (!m_oc) {
        LOGE << "Could not deduce output format from file extension: using MPEG.";
        avformat_alloc_output_context2(&m_oc, nullptr, "mpeg", m_par.filePath.c_str());
    }
    if (!m_oc) {
        throw runtime_error("Could create context");
    }
}

void FFMpegEncode::addStreams() {
    // Add the audio and video streams using the default m_format codecs and initialize the codecs.
    addStream(&m_outStream[toType(streamType::video)], m_oc);
    m_have[toType(streamType::video)] = true;

    if (!m_noAudio) {
        addStream(&m_outStream[toType(streamType::audio)], m_oc);
        m_have[toType(streamType::audio)] = true;
    }
}

void FFMpegEncode::addStream(OutputStream *ost, AVFormatContext *oc) {
    if (m_forceCodec.size() > 1) {
        ost->codec = avcodec_find_encoder_by_name(m_forceCodec.c_str());
        if (!ost->codec) {
            throw runtime_error("couldn't find encoder: " + m_forceCodec);
        }
    }

    // find the encoder
    if (!ost->codec) {
        auto codecId = m_par.fromDecoder ? m_par.fromDecoder->getDecoderCtx()->codec_id : m_oc->oformat->video_codec;
        ost->codec = avcodec_find_encoder(codecId);
        if (!ost->codec) {
            throw runtime_error("Could not find encoder for "+std::string(avcodec_get_name(codecId)));
        }
    }

    if (oc) {
        ost->st = avformat_new_stream(oc, nullptr);
        if (!ost->st) {
            throw runtime_error("Could not allocate stream");
        }
        //ost->st->id = static_cast<int32_t>(oc->nb_streams) -1;
    }

    /*int ret = 0;
    if ((ret = avformat_init_output(oc, nullptr)) < 0) {
        throw runtime_error("avformat_init_output error "+ffmpeg::err2str(m_ret));
    }*/

    ost->enc = avcodec_alloc_context3(ost->codec);
    if (!ost->enc) {
        throw runtime_error("Could not alloc an encoding context");
    }

    switch (ost->codec->type) {
        case AVMEDIA_TYPE_AUDIO:
#ifdef WITH_AUDIO
            c->sample_fmt  = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = m_audioBitRate;
        c->sample_rate = pa->getSampleRate();
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == pa->getSampleRate())
                    c->sample_rate = pa->getSampleRate();
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        av_opt_set(c->priv_data, "preset", "fastest", 0);
#endif
            break;
        case AVMEDIA_TYPE_VIDEO:
            setVideoCodePar(ost);
            break;
        default:
            break;
    }

    // Some formats want stream headers to be separate.
    if (oc && oc->oformat->flags & AVFMT_GLOBALHEADER) {
        ost->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}

void FFMpegEncode::openStreams() {
    // Now that all the parameters are set, we can open the audio and
    // video codecs and allocate the necessary encode buffers.
    if (m_have[toType(streamType::video)]) {
        openVideo(&m_outStream[toType(streamType::video)], m_opt);
    }

    if (m_have[toType(streamType::audio)]) {
        openAudio(&m_outStream[toType(streamType::audio)], m_opt);
    }
}

void FFMpegEncode::openOutputFile(AVDictionary *avioOpts) const {
    // open the output file, if needed
    if (!(m_oc->oformat->flags & AVFMT_NOFILE)
        && avio_open(&m_oc->pb, m_par.filePath.c_str(), AVIO_FLAG_WRITE)< 0) {
        throw runtime_error("Could not open '" +m_par.filePath +"': " +ffmpeg::err2str(m_ret));
    }

    // Write the stream header, if any. Note: This might change the streams time_base
    if (avformat_write_header(m_oc, avioOpts ? &avioOpts : nullptr) < 0) {
        throw runtime_error("Error occurred when opening output file: " +ffmpeg::err2str(m_ret));
    }
    av_dump_format(m_oc, 0, m_par.filePath.c_str(), 1);
}

void FFMpegEncode::recThread() {
    m_recCond.notify();

    while (m_doRec) {
        if (m_noAudio) {
            if (m_videoFrames.getFillAmt() > 0) {
                writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
            } else {
                this_thread::sleep_for(200us);
            }
        } else {
            // select the stream to encode if video is before audio, write video, otherwise audio
            if (av_compare_ts(m_outStream[toType(streamType::video)].next_pts, m_outStream[toType(streamType::video)].enc->time_base,
                              m_outStream[toType(streamType::audio)].next_pts, m_outStream[toType(streamType::audio)].enc->time_base) <= 0) {
                writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
            } else {
                writeAudioFrame(m_oc, &m_outStream[toType(streamType::audio)], false);
            }
        }
    }

    LOG << "FFMpegEncode stop, remaining video frames: " << m_videoFrames.getFillAmt();

    // save rest of pics m_buffer
    while (m_videoFrames.getFillAmt() > 0) {
        try {
            writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
        } catch(std::runtime_error& e) {
            LOGE << "FFMpegEncode Error: " << e.what();
        }
    }

    // save rest of audio m_buffer
    while (!m_audioQueue.empty()) {
        writeAudioFrame(m_oc, &m_outStream[toType(streamType::audio)], true);
    }

    // Write the trailer, if any. The trailer must be written before you
    // close the CodecContexts open when you wrote the header; otherwise
    // av_codec_close().
    av_write_trailer(m_oc);

    cleanUp();
}

void FFMpegEncode::cleanUp() {
    for (int i = 0; i < toType(streamType::size); ++i) {
        if (m_have[i]) {
            closeStream(m_oc, &m_outStream[i]);
        }
    }

    // Close the output file.
    if (!(m_oc->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&m_oc->pb);
    }

    // free the stream
    avformat_free_context(m_oc);
    m_oc = nullptr;

    if (m_par.useHwAccel) {
        if (m_hw_device_ctx){
            av_buffer_unref(&m_hw_device_ctx);
            m_hw_device_ctx = nullptr;
        }
        if (m_hw_frame){
            av_frame_free(&m_hw_frame);
            m_hw_frame = nullptr;
        }
    }

    m_videoFrames.clear();
    m_stopCond.notify();
}

int FFMpegEncode::setHwframeCtx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx) {
    AVBufferRef *hw_frames_ref = nullptr;
    AVHWFramesContext *frames_ctx = nullptr;
    int err = 0;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
        LOGE <<  "Failed to create hw frame context.";
        return -1;
    }

    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = m_hwPixFmt;
    frames_ctx->sw_format = m_hwSwFmt;
    frames_ctx->width     = m_par.width;
    frames_ctx->height    = m_par.height;
    frames_ctx->initial_pool_size = 16;

    if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
        //LOGE << " Failed to initialize hw frame context. Error code: " << ffmpeg::err2str(err);
        av_buffer_unref(&hw_frames_ref);
        return err;
    }

    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!ctx->hw_frames_ctx) {
        err = AVERROR(ENOMEM);
    }

    av_buffer_unref(&hw_frames_ref);
    return err;
}

void FFMpegEncode::setVideoCodePar(OutputStream *ost) {
    if (m_par.fromDecoder) {
        auto dec_ctx = m_par.fromDecoder->getDecoderCtx();
        ost->enc->height = dec_ctx->height;
        ost->enc->width = dec_ctx->width;
        ost->enc->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
        // take first format from list of supported formats

        if (ost->codec->pix_fmts) {
            ost->enc->pix_fmt = ost->codec->pix_fmts[0];
        } else {
            ost->enc->pix_fmt = dec_ctx->pix_fmt;
        }
        //ost->enc->pix_fmt = m_par.useHwAccel ? m_hwPixFmt : AV_PIX_FMT_YUV420P;


        // video time_base can be set to whatever is handy and supported by encoder
        ost->enc->time_base = av_inv_q(dec_ctx->framerate);

/*      auto st = m_par.fromDecoder->getStream(streamType::video);
        if (avcodec_parameters_to_context(ost->enc, st->codecpar) < 0) {
            avcodec_free_context(&ost->enc);
            throw runtime_error("Could not copy codec parameters to decoder context.");
        }*/

      /*  c->time_base = st->time_base;

        ost->st->time_base = st->time_base; // tbn
        ost->st->avg_frame_rate = st->avg_frame_rate; // tbr, needed e.g. for rtmp streams
        ost->st->r_frame_rate = st->r_frame_rate; // tbr, needed e.g. for rtmp streams*/
    } else {
        ost->enc->codec_id = ost->codec->id;
        ost->enc->bit_rate = m_par.videoBitRate;
        ost->enc->width = m_par.width; // Resolution must be a multiple of two.
        ost->enc->height = m_par.height;
        ost->enc->pix_fmt = m_par.useHwAccel ? m_hwPixFmt : AV_PIX_FMT_YUV420P;
        ost->enc->gop_size = m_isRtmp ? 25 : 10; // obs setting gop -> group of pictures
        ost->enc->time_base = AVRational{1, m_par.fps};
        ost->enc->framerate = AVRational{m_par.fps, 1};
    }

    //ost->st->time_base = m_par.useHwAccel ? AVRational{1, m_par.fps} : AVRational{1, 1000};

    //c->time_base = ost->st->time_base;
/*
    if (m_par.useHwAccel && (m_ret = setHwframeCtx(ost->enc, m_hw_device_ctx)) < 0) {
        throw runtime_error("Failed to set hwframe context.");
    }

    if (!m_par.fromDecoder && (ost->enc->codec_id == AV_CODEC_ID_H264 || ost->enc->codec_id == AV_CODEC_ID_MPEG2TS)) {
        // film, animation, grain, stillimage, psnr, ssim, fastdecode, zerolatency
        if (m_is_net_stream) {
            //av_opt_set(c->priv_data, "preset", "superfast", 0);
            av_opt_set(ost->enc->priv_data, "vbr", "1", AV_OPT_SEARCH_CHILDREN);
        } else {
            //av_opt_set(c->priv_data, "tune", "animation", 0);
            // ultrafast,superfast, veryfast, faster, fast, medium, slow, slower, veryslow
            // je schneller, desto groesser die dateien
            //av_opt_set(c->priv_data, "preset", "faster", 0);
        }

        // for compatibility
        ost->enc->profile = FF_PROFILE_H264_BASELINE;
        ost->enc->me_cmp = FF_CMP_CHROMA;
        //	c->me_method = ME_EPZS;
    }

    if (!m_par.fromDecoder && ost->enc->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        // just for testing, we also add B-frames
        ost->enc->max_b_frames = 2;
    }

    if (!m_par.fromDecoder && ost->enc->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        // Needed to avoid using macroblocks in which some coeffs overflow.
        // This does not happen with normal video, it just happens here as
        // the motion of the chroma plane does not match the luma plane.
        ost->enc->mb_decision = 2;
    }

    bool twopass = false;
    const char *preset = "superfast"; // "mq";
    const char *profile = "high";
    const char *rc = m_isRtmp ? "VBR" : "CBR";
    int cqp = 20;

    av_opt_set_int(ost->enc->priv_data, "cbr", false, 0);
    av_opt_set(ost->enc->priv_data, "profile", profile, 0);
    //av_opt_set(ost->enc->priv_data, "preset", preset, 0);

    if (strcmp(rc, "cqp") == 0) {
        m_par.videoBitRate = 0;
        ost->enc->global_quality = cqp;
    } else if (strcmp(rc, "lossless") == 0) {
        m_par.videoBitRate = 0;
        cqp =  m_par.videoBitRate;
        bool hp = (strcmp(preset, "hp") == 0 || strcmp(preset, "llhp") == 0);
        av_opt_set(ost->enc->priv_data, "preset", hp ? "losslesshp" : "lossless", 0);
    } else if (strcmp(rc, "vbr") != 0) { // CBR by default
        av_opt_set_int(ost->enc->priv_data, "cbr", false, 0);
        av_opt_set_int(ost->enc->priv_data, "vbr", true, 0);
        ost->enc->rc_max_rate = ost->enc->bit_rate;
        ost->enc->rc_min_rate = ost->enc->bit_rate;
        cqp = 0;
    }

    //av_opt_set(ost->enc->priv_data, "level", "auto", 0);
    av_opt_set_int(ost->enc->priv_data, "2pass", twopass, 0);
    //av_opt_set_int(ost->enc->priv_data, "gpu", gpu, 0);
    //set_psycho_aq(enc, psycho_aq);

    ost->enc->rc_buffer_size = ost->enc->bit_rate;

    printf("settings:\n"
           "\trate_control: %s\n"
           "\tframerate:    %f\n"
           "\tbitrate:      %ld\n"
           "\tcqp:          %d\n"
           "\tkeyint:       %d\n"
           "\tpreset:       %s\n"
           "\tprofile:      %s\n"
           "\twidth:        %d\n"
           "\theight:       %d\n"
           "\t2-pass:       %s\n"
           "\tb-frames:     %d\n",
            //"\tpsycho-aq:    %d\n"
            //"\tGPU:          %d\n",
           rc, r2d(ost->enc->framerate), ost->enc->bit_rate, cqp, ost->enc->gop_size, preset, profile,
           ost->enc->width, ost->enc->height, twopass ? "true" : "false", ost->enc->max_b_frames
            //psycho_aq,
            //gpu
    );

    if (m_ret < 0) {
        throw runtime_error("Error could not set rtmp option");
    }

    if (m_isRtmp) {
        ost->st->time_base = AVRational{ 1, 1000 }; // tbn
        ost->st->avg_frame_rate = AVRational{m_par.fps, 1}; // tbr, needed e.g. for rtmp streams
        ost->st->r_frame_rate = AVRational{m_par.fps, 1}; // tbr, needed e.g. for rtmp streams
        ost->st->codecpar->extradata = ost->enc->extradata;
        ost->st->codecpar->extradata_size = ost->enc->extradata_size;
        if (m_ret < 0) {
            throw runtime_error("Error could not set rtmp option");
        }
    }*/
}

void FFMpegEncode::openAudio(OutputStream *ost, AVDictionary *opt_arg) {
    AVCodecContext *c;
    int nb_samples;
    AVDictionary *opt = nullptr;
    c = ost->enc;

    // open it
    av_dict_copy(&opt, opt_arg, 0);
    auto ret = avcodec_open2(c, ost->codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        //LOGE << "Could not open audio codec: " << ffmpeg::err2str(ret);
        return;
    }

    m_src_nb_samples = (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) ? 10000 :c->frame_size;
    nb_samples = m_src_nb_samples;

    ret = av_samples_alloc_array_and_samples(&m_src_samples_data, &m_src_samples_linesize, c->ch_layout.nb_channels,
                                             m_src_nb_samples, c->sample_fmt, 0);
    if (ret < 0) {
        LOGE << "Could not allocate source samples";
        return;
    }

    ost->frame     = allocAudioFrame(c->sample_fmt, c->ch_layout, c->sample_rate, nb_samples);
    ost->tmp_frame = allocAudioFrame(AV_SAMPLE_FMT_S16, c->ch_layout, c->sample_rate, nb_samples);

    // copy the stream parameters to the muxer
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        LOGE << "Could not copy the stream parameters";
        return;
    }

    // create resampler context
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        LOGE << "Could not allocate resampler context";
        return;
    }

    // set options
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->ch_layout.nb_channels, 0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,           0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16,        0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->ch_layout.nb_channels, 0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,           0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,            0);

    // initialize the resampling context
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        LOGE << "Failed to initialize the resampling context";
        return;
    }

    // compute the number of converted samples: buffering is avoided
    // ensuring that the output m_buffer will contain at least all the
    // converted input samples
    m_max_dst_nb_samples = m_src_nb_samples;
    ret = av_samples_alloc_array_and_samples(&m_dst_samples_data, &m_dst_samples_linesize, c->ch_layout.nb_channels,
                                             m_max_dst_nb_samples, c->sample_fmt, 0);
    if (ret < 0) {
        LOGE << "Could not allocate destination samples";
        return;
    }

    m_dst_samples_size = av_samples_get_buffer_size(nullptr, c->ch_layout.nb_channels, m_max_dst_nb_samples, c->sample_fmt, 0);
}

AVFrame* FFMpegEncode::getAudioFrame(OutputStream *ost, bool clear) {
    AVFrame *frame = ost->tmp_frame;
    auto *q = reinterpret_cast<int16_t*>(frame->data[0]);
    int newSize = 0;

    if (static_cast<int32_t>(m_audioQueue.size()) >= frame->nb_samples * ost->enc->ch_layout.nb_channels) {
        for (int j=0;j<frame->nb_samples;j++) {
            for (int chanNr = 0; chanNr < ost->enc->ch_layout.nb_channels; chanNr++) {
                *q++ = static_cast<int16_t>(m_audioQueue[j * ost->enc->ch_layout.nb_channels + chanNr] * 32767);
            }
        }

        newSize = static_cast<int32_t>(m_audioQueue.size()) - frame->nb_samples * ost->enc->ch_layout.nb_channels;
        m_audioQueue.erase(m_audioQueue.begin(), m_audioQueue.begin() + frame->nb_samples * ost->enc->ch_layout.nb_channels);
        m_audioQueue.resize(newSize);

    } else if (clear) {
        for (unsigned int j=0; j<(m_audioQueue.size() / ost->enc->ch_layout.nb_channels); j++) {
            for (int chanNr = 0; chanNr < ost->enc->ch_layout.nb_channels; chanNr++) {
                *q++ = static_cast<int16_t>(m_audioQueue[j * ost->enc->ch_layout.nb_channels + chanNr] * 32767);
            }
        }

        // add zeros to complete the frame_size
        for (unsigned int j=0; j<frame->nb_samples - (m_audioQueue.size() / ost->enc->ch_layout.nb_channels); j++) {
            for (int chanNr = 0; chanNr < ost->enc->ch_layout.nb_channels; chanNr++) {
                *q++ = 0;
            }
        }

        m_audioQueue.clear();
        m_audioQueue.resize(0);
    }

    frame->pts = ost->next_pts;
    ost->next_pts  += frame->nb_samples;

    return frame;
}

// encode one audio frame and send it to the muxer return 1 when encoding is finished, 0 otherwise
int FFMpegEncode::writeAudioFrame(AVFormatContext *oc, OutputStream *ost, bool clear) {
    AVFrame* frame{};
    AVCodecContext* c = ost->enc;
    int ret;
    int got_packet=0;
    int dst_nb_samples;

    if (!m_audioQueue.empty()) {
        auto pkt = av_packet_alloc();
        frame = getAudioFrame(ost, clear);
        if (frame) {
            // convert samples from native m_format to destination codec m_format, using the resampler
            // compute destination number of samples
            dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
                                              c->sample_rate, c->sample_rate, AV_ROUND_UP);
            av_assert0(dst_nb_samples == frame->nb_samples);

            // when we pass a frame to the encoder, it may keep a reference to it internally;
            // make sure we do not overwrite it here
            ret = av_frame_make_writable(ost->frame);
            if (ret < 0)
                exit(1);

            // convert to destination m_format
            ret = swr_convert(ost->swr_ctx, ost->frame->data, dst_nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
            if (ret < 0) {
                LOGE << "Error while converting";
                return -1;
            }

            frame = ost->frame;

            AVRational initRat;
            initRat.num = 1;
            initRat.den = c->sample_rate;
            frame->pts = av_rescale_q(ost->samples_count, initRat, c->time_base);

            ost->samples_count += dst_nb_samples;
        } else {
            LOGE << "Couldnt get audio frame";
        }

        // send the frame for encoding
        ret = avcodec_send_frame(c, frame);
        if (ret < 0) {
            LOGE <<  "Error sending the frame to the encoder";
            return 0;
        }

        // read all the available output packets (in general there may be any
        // number of them
        while (ret >= 0) {
            ret = avcodec_receive_packet(c, pkt);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                this_thread::sleep_for(200us);
                return 0;
            } else if (ret < 0) {
                LOGE <<  "Error encoding audio frame";
                return 0;
            }

            ret = writePacket(oc, ost, pkt);
            if (ret < 0) {
                //LOGE << "Error while writing audio frame: " << ffmpeg::err2str(ret);
                return -1;
            }

        }
    }

    return (frame || got_packet) ? 0 : 1;
}

void FFMpegEncode::openVideo(OutputStream *ost, AVDictionary *opt_arg) {
    AVDictionary *opt = nullptr;
    av_dict_copy(&opt, opt_arg, 0);
    m_av_interleaved_wrote_first = false;

    // open the codec
    //auto ret = avcodec_open2(ost->enc, ost->codec, &opt);
    auto ret = avcodec_open2(ost->enc, ost->codec, nullptr);
    av_dict_free(&opt);
    if (ret < 0) {
        throw runtime_error("Could not open video codec: "+ ffmpeg::err2str(ret));
    }

    // allocate and init a re-usable frame
    ost->frame = allocPicture(m_par.useHwAccel ? m_hwSwFmt : ost->enc->pix_fmt, ost->enc->width, ost->enc->height);
    if (!ost->frame) {
        throw runtime_error("Could not allocate video frame");
    }

    // allocate a frame for filtering
    if (m_useFiltering) {
        m_filt_frame = allocPicture(ost->enc->pix_fmt, ost->enc->width, ost->enc->height);
    }

    // copy the stream parameters to the muxer
    ret = avcodec_parameters_from_context(ost->st->codecpar, ost->enc);
    if (ret < 0) {
        throw runtime_error("Could not copy the stream parameters");
    }

    ost->st->time_base = ost->enc->time_base;

    // Allocate an AVFrame structure
    m_inpFrame = allocPicture(m_par.pixelFormat, m_par.width, m_par.height);
    m_frameBGRA = allocPicture(AV_PIX_FMT_BGRA, m_par.width, m_par.height);

    if (m_par.useHwAccel) {
        if (!((m_hw_frame = av_frame_alloc()))) {
            m_ret = AVERROR(ENOMEM);
            LOGE << "Couldn't allocate hw frame. No memory left";
            return;
        }

        if ((m_ret = av_hwframe_get_buffer(ost->enc->hw_frames_ctx, m_hw_frame, 0)) < 0) {
            //LOGE << "Error code: " << ffmpeg::err2str(m_ret);
            return;
        }

        if (!m_hw_frame->hw_frames_ctx) {
            m_ret = AVERROR(ENOMEM);
            LOGE << "Couldn't allocate hw frame. No memory left";
        }
    }
}

// encode one video frame and send it to the muxer return 1 when encoding is finished, 0 otherwise
int FFMpegEncode::writeVideoFrame(AVFormatContext *oc, OutputStream *ost) {
    auto encFrame = ost->frame;
    if (!encFrame) {
        throw runtime_error("encFrame not allocated");
    }

    // only write if there was a frame downloaded
    if (m_useFiltering) {
        // allocate a frame with the settings of the context
        encFrame = av_frame_alloc();
        if (!encFrame) {
            throw runtime_error("error copying frame to encFrame");
        }

        av_image_alloc(encFrame->data, encFrame->linesize, ost->enc->width, ost->enc->height,
                       ost->enc->pix_fmt, 1);
        encFrame->width = ost->enc->width;
        encFrame->height = ost->enc->height;
        encFrame->pict_type = AV_PICTURE_TYPE_NONE; // reset picture type for having encoder setting it
        encFrame->format = ost->enc->pix_fmt;
    }

    // copy raw rgb buffer to AVFrame
    // check if the frame to encode is a reference to an external buffer or a regular recQueue.buffer
    if (m_videoFrames.getReadBuff().bufferPtr) {
        m_ret = av_image_fill_arrays(m_inpFrame->data, m_inpFrame->linesize,
                                     m_videoFrames.getReadBuff().bufferPtr, m_par.pixelFormat, m_par.width, m_par.height, 1);
        m_videoFrames.getReadBuff().bufferPtr = nullptr;
    } else {
        m_ret = av_image_fill_arrays(m_inpFrame->data, m_inpFrame->linesize,
                                     m_videoFrames.getReadBuff().buffer.data(), m_par.pixelFormat, m_par.width,
                                     m_par.height, 1);
    }
    //m_inpFrame->pts = m_videoFrames.getReadBuff().pts;
    m_videoFrames.consumeCountUp();

    // doing screencast from a display with 60fps results in jittering playback, since the monitors framerate is not exactly constant
    // using libx264 with a high resolution time-base and directly using the elapsed time as pts gives the best results (e.m_g. monitor 60fps to 30fps encoding)
    // unfortunately with NVENC this doesn't work...
    ost->frame->pts = ost->next_pts++;  // fixed frameduration
    if (m_par.useHwAccel) {
        m_hw_frame->pts = ost->frame->pts;
    }

    auto encPixFmt = m_par.useHwAccel ? m_hwSwFmt : ost->enc->pix_fmt;

    // flip image horizontally
    if (m_flipH) {
        // setup a sws context. this checks if the context can be reused
        m_converter = sws_getCachedContext(m_converter,
                                            m_par.width,	    // srcwidth
                                            m_par.height,	    // srcheight
                                            m_par.pixelFormat,	// src_format
                                            ost->enc->width,	// dstWidth
                                            ost->enc->height,	// dstHeight
                                            encPixFmt,	        // dst_format
                                            m_sws_flags,		// flags
                                            nullptr,			// srcFilter
                                            nullptr,		    // dstFilter
                                            nullptr			    // param
                                            );

        *m_inpFrame->data += m_par.width * m_glNrBytesPerPixel * (m_par.height - 1);
        for (int i=0;i<8;i++) {
            m_stride[i] = -m_inpFrame->linesize[i];
        }
    } else {
        memcpy(m_stride, m_inpFrame->linesize, 8);
    }

    if (m_useFiltering) {
        sws_scale(m_converter, m_inpFrame->data, m_stride, 1,
                  ost->enc->height, ost->frame->data, ost->frame->linesize);

        // push the frame into the filtergraph
        // killt den frame
        m_ret = av_buffersrc_add_frame(m_buffersrc_ctx, encFrame);
        if (m_ret != 0) {
            throw runtime_error("Error while feeding the filtergraph. error code " + std::to_string(m_ret));
        }

        if (av_buffersink_get_frame(m_buffersink_ctx, m_filt_frame) < 0) {
            throw runtime_error("error filtering frame");
        }

        //m_filt_frame->pts = m_inpFrame->pts;
    } else {
        // combination of libyuv::ARGBToNV12 take around 2 ms on an i7-7700HQ CPU @ 2.80GHz for an HD image
        // in comparison to 7 ms with sws_scale

        // Note: libyuv always take planes in reverse order: ffmpeg BGRA corresponds to libyuv ARGB
        if (encPixFmt == AV_PIX_FMT_NV12) {
            auto inFrame = m_inpFrame;

            if (m_par.pixelFormat == AV_PIX_FMT_RGB24) {
                libyuv::RAWToARGB(m_inpFrame->data[0], m_stride[0],
                                  m_frameBGRA->data[0], m_frameBGRA->linesize[0],
                                  m_par.width, m_par.height);
                memcpy(m_stride, m_frameBGRA->linesize, 8);
                inFrame = m_frameBGRA;
            }

            libyuv::ARGBToNV12(inFrame->data[0], m_stride[0],
                               ost->frame->data[0], ost->frame->linesize[0],
                               ost->frame->data[1], ost->frame->linesize[1],
                               m_par.width, m_par.height);

        } else if (encPixFmt == AV_PIX_FMT_YUV420P) {
            if (m_par.pixelFormat == AV_PIX_FMT_RGB24) {
                libyuv::RAWToI420(m_inpFrame->data[0], m_stride[0],
                                   ost->frame->data[0], ost->frame->linesize[0],
                                   ost->frame->data[1], ost->frame->linesize[1],
                                   ost->frame->data[2], ost->frame->linesize[2],
                                   m_par.width,  m_par.height);
            } else {
                libyuv::ARGBToI420(m_inpFrame->data[0], m_stride[0],
                                   ost->frame->data[0], ost->frame->linesize[0],
                                   ost->frame->data[1], ost->frame->linesize[1],
                                   ost->frame->data[2], ost->frame->linesize[2],
                                   m_par.width,  m_par.height);
            }
        }
    }

    if (m_par.useHwAccel) {
        if ((m_ret = av_hwframe_transfer_data(m_hw_frame, ost->frame, 0)) < 0) { // around 0.22 ms for a HD
            throw runtime_error("av_hwframe_transfer_data failed");
        }
        encFrame = m_hw_frame;
    }

    auto decPkt = av_packet_alloc();
    if (!decPkt) {
        throw std::runtime_error("Could not alloc packet");
    }

    if (!m_par.useHwAccel) {
        m_ret = av_frame_make_writable(ost->frame);
    }

    // send the frame to the encoder
    auto frame = m_useFiltering ? m_filt_frame : encFrame;
  //  LOG << frame->pts;

    m_ret = avcodec_send_frame(ost->enc, frame);
    if (m_ret < 0) {
        av_packet_unref(decPkt);
        av_packet_free(&decPkt);
        throw std::runtime_error("Error sending a frame for encoding");
    }

    while (m_ret >= 0) {
        m_ret = avcodec_receive_packet(ost->enc, decPkt);
        if (m_ret == AVERROR(EAGAIN) || m_ret == AVERROR_EOF) {
            break;
        } else if (m_ret < 0) {
            throw std::runtime_error("Error during encoding");
            //break;
        }

        m_ret = writePacket(oc, ost, decPkt); // paket is freed implicitly
    }

    av_packet_unref(decPkt);
    av_packet_free(&decPkt);
    return encFrame ? 0 : 1;
}

int FFMpegEncode::writePacket(AVFormatContext *fmt_ctx, OutputStream *ost, AVPacket *pkt) {
#if defined(ARA_USE_LIBRTMP) && defined(_WIN32)
    if (m_isRtmp) {
        if (m_rtmpSender.first_packet) {
            LOG << "got first packet!";
            auto newPacket = m_rtmpSender.extract_avc_headers(pkt->data, pkt->size, util::OBS_ENCODER_VIDEO);
            m_rtmpSender.first_packet = false;
            m_rtmpSender.getStream().writePos++;
        }
    }
#endif

    // rescale output packet timestamp values from codec to stream timebase
    av_packet_rescale_ts(pkt, ost->enc->time_base,  ost->st->time_base);
    pkt->stream_index = ost->st->index;

    // Write the compressed frame to the media file.
    // NOTE: in case of network streams av_interleaved_write_frame will wait until the packet reached and thus contain the network delay
    auto r = av_interleaved_write_frame(fmt_ctx, pkt);
    if (r < 0) {
        LOGE << "av_interleaved_write_frame error";
    }

    if (!m_av_interleaved_wrote_first) {
        m_av_interleaved_wrote_first = true;
    }

    return r;
}

void FFMpegEncode::closeStream(AVFormatContext *oc, FFMpegEncode::OutputStream *ost) {
    avcodec_free_context(&ost->enc);
    av_frame_free(&m_inpFrame); m_inpFrame = nullptr;
    av_frame_free(&m_frameBGRA); m_frameBGRA = nullptr;
    av_frame_free(&ost->frame); ost->frame = nullptr;
    av_frame_free(&ost->tmp_frame); ost->tmp_frame = nullptr;
    if (m_useFiltering){
        av_frame_free(&m_filt_frame);
        m_filt_frame = nullptr;
    }
    sws_freeContext(ost->sws_ctx); ost->sws_ctx = nullptr;
    swr_free(&ost->swr_ctx); ost->swr_ctx = nullptr;
}

/// fixFps: force monotonic timestamps,
/// bufPtr: optionally download to an external buffer
void FFMpegEncode::downloadGlFbToVideoFrame(double fixFps, unsigned char* bufPtr, bool monotonic, int64_t pts) {
    //LOG << "receive pts " << pts;

    if (!m_doRec) {
        return;
    }

    auto now = chrono::system_clock::now();
    if (!m_gotFirstFrame) {
        m_startEncTime = now;
        m_gotFirstFrame = true;
        m_encTimeDiff = std::numeric_limits<double>::max();
        m_lastEncTime = now;
    } else {
        m_encTimeDiff = std::chrono::duration<double, std::milli>(now - m_lastEncTime).count();
        m_encElapsedTime = std::chrono::duration<double, std::milli>(now - m_startEncTime).count();
    }

    // if the next frame comes earlier than fixFps would allow, skip it
    if (((m_par.useHwAccel && fixFps != 0.0) && m_encTimeDiff < m_frameDur * 0.75 && !monotonic)
        || (fixFps == 0.0 && (m_frameDur - m_encTimeDiff) > (m_frameDur * 0.5) && !monotonic)) {
        return;
    }

    while (m_videoFrames.isFilled()) {
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // skip frame and wait
    }

    bool gotData = false;
    if (!bufPtr) {
        if (m_pbos.getFillAmt() > 0 && !m_videoFrames.isFilled()) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos.getReadBuff());
#ifdef ARA_USE_GLES31
            auto ptr = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_nbytes, GL_MAP_READ_BIT));
#else
            auto ptr = static_cast<uint8_t *>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
#endif
            if (ptr) {
                memcpy(m_videoFrames.getWriteBuff().buffer.data(), ptr, m_nbytes);
                m_videoFrames.getWriteBuff().bufferPtr = m_videoFrames.getWriteBuff().buffer.data();
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            } else {
                LOGE << "Failed to map the m_buffer";
            }
            m_pbos.consumeCountUp();
            gotData = true;
        }

        // write pbo with actual data
        if (!m_pbos.isFilled()) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos.getWriteBuff());
            glReadPixels(0, 0, m_par.width, m_par.height, m_glDownloadFmt, GL_UNSIGNED_BYTE, nullptr);
            m_pbos.feedCountUp();
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    } else {
        m_videoFrames.getWriteBuff().bufferPtr = bufPtr;
        gotData = true;
    }

    // directly write the buffer as a png for debugging reasons
    if (m_savePngSeq) {
        savePngSeq();
    }

    if (gotData) {
        m_videoFrames.getWriteBuff().encTime = m_encElapsedTime; // not used at the moment
        m_videoFrames.getWriteBuff().pts = m_fakeCntr++;
        m_videoFrames.feedCountUp();
    }

    m_lastEncTime =  chrono::system_clock::now();
}

void FFMpegEncode::savePngSeq() {
    if (m_savePngFirstCall) {
        if (filesystem::exists(m_par.downloadFolder)) {
            filesystem::remove_all(m_par.downloadFolder);
        }

        if (!filesystem::exists(m_par.downloadFolder)) {
            filesystem::create_directory(m_par.downloadFolder);
        }
        m_savePngFirstCall = false;
    }


    if (m_videoFrames.getWriteBuff().bufferPtr) {
        auto bPtr = m_videoFrames.getWriteBuff().bufferPtr;
        auto seqPtr = m_pngSeqCnt;
        Texture::saveBufToFile2D((m_par.downloadFolder+"/seq_"
                                    +std::to_string(seqPtr / 1000 % 10)
                                    +std::to_string(seqPtr / 100 % 10)
                                    +std::to_string(seqPtr / 10 % 10)
                                    +std::to_string(seqPtr % 10)+".tga").c_str(),
                                   FIF_TARGA,
                                   m_par.width,
                                   m_par.height,
                                   4,
                                   bPtr);
        m_pngSeqCnt++;
    }
}

#ifdef WITH_AUDIO
void FFMpegEncode::mediaRecAudioDataCallback(PAudio::paPostProcData* paData) {
    float mixSamp = 0.f;

    // write interleaved
    // if the number of channels of the codec is the same as the actual number of channels of paudio,
    // just copy
    if (paData->numChannels == paData->codecNrChans)
    {
        for (unsigned int samp=0; samp<paData->frameCount; samp++)
            for (unsigned int chan=0; chan<paData->numChannels; chan++)
                paData->inSampQ->push_back( paData->sampData[chan][paData->offset + samp] );
    } else
    {
        // mix down
        for (unsigned int samp=0; samp<paData->frameCount; samp++)
            for (unsigned int chan=0; chan<paData->codecNrChans; chan++)
            {
                mixSamp = 0.f;
                for ( unsigned int i=0;i< paData->m_mixDownMap->at(chan).size();i++)
                    mixSamp += paData->sampData[ paData->m_mixDownMap->at(chan)[i] ][paData->offset + samp];

                paData->inSampQ->push_back( mixSamp );
            }
    }
}
#endif

void FFMpegEncode::freeGlResources() {
    glDeleteBuffers((GLsizei)m_num_pbos, m_pbos.getBuffer().data());
    m_pbos.clear();
}

}

#endif