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

using namespace std;
using namespace ara::av::ffmpeg;

namespace ara::av {

FFMpegEncode::FFMpegEncode(const EncodePar& par) {
    init(par);
}

bool FFMpegEncode::init(const EncodePar& par) {
    m_par = par;
    m_frameDur = 1000.0 / par.fps; // in ms

    initFFMpeg();

    m_glDownloadFmt = getGlColorFormatFromAVPixelFormat(par.pixelFormat);
    m_glNrBytesPerPixel = getNumBytesPerPix(m_glDownloadFmt);
    m_nbytes = par.width * par.height * m_glNrBytesPerPixel;

    // create buffers for downloading images from opengl
    m_videoFrames.allocate(m_nrBufferFrames);
    for (auto &it : m_videoFrames.getBuffer()){
        it.buffer.resize(m_nbytes);
        it.encTime = -1.0;
    }

    m_pbos.allocate(m_num_pbos);
    glGenBuffers(static_cast<GLsizei>(m_num_pbos), m_pbos.getBuffer().data());
    for (auto &it : m_pbos.getBuffer()) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, it);
        glBufferData(GL_PIXEL_PACK_BUFFER, m_nbytes, NULL, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    m_inited = true;
    return true;
}

bool FFMpegEncode::record() {
    for (auto& it: m_outStream) {
        it = {0};
    }

    m_fileType = std::filesystem::path(m_par.filePath).extension().string();

    AVDictionary* avioOpts = nullptr;

    if (m_par.filePath.substr(0, 6) == "mms://" || m_par.filePath.substr(0, 7) == "mmsh://" ||
    m_par.filePath.substr(0, 7) == "mmst://" || m_par.filePath.substr(0, 7) == "mmsu://" ||
    m_par.filePath.substr(0, 7) == "http://" || m_par.filePath.substr(0, 8) == "https://" ||
    m_par.filePath.substr(0, 7) == "rtmp://" || m_par.filePath.substr(0, 6) == "udp://" ||
    m_par.filePath.substr(0, 7) == "rtsp://" || m_par.filePath.substr(0, 6) == "rtp://" ||
    m_par.filePath.substr(0, 6) == "ftp://" || m_par.filePath.substr(0, 7) == "sftp://" ||
    m_par.filePath.substr(0, 6) == "tcp://" || m_par.filePath.substr(0, 7) == "unix://" ||
    m_par.filePath.substr(0, 6) == "smb://")
    {
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
            m_isRtmp=true;
        }

        m_is_net_stream = true;
    }

    LOG << "FFMpegEncode recording to " << m_par.filePath;

    if (m_par.useHwAccel) {
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

    if (m_forceCodec.size() > 1) {
        m_codec[toType(streamType::video)] = avcodec_find_encoder_by_name(m_forceCodec.c_str());
        if (!m_codec[toType(streamType::video)]){
            LOGE << "FFMpegEncode error: couldn't find encoder: " << m_forceCodec;
            return false;
        }
    }

    if (m_isRtmp) {
        avformat_alloc_output_context2(&m_oc, nullptr, "flv", m_par.filePath.c_str()); //RTMP
    } else {
        avformat_alloc_output_context2(&m_oc, nullptr, nullptr, m_par.filePath.c_str());
    }

    if (!m_oc) {
        LOGE << "Could not deduce output format from file extension: using MPEG.";
        avformat_alloc_output_context2(&m_oc, nullptr, "mpeg", m_par.filePath.c_str());
    }

    if (!m_oc) {
        LOGE << "FFMpegEncode error: Could create context.";
    }

    m_fmt = m_oc->oformat;

    // Add the audio and video streams using the default m_format codecs
    // and initialize the codecs.
    if ((m_fmt && m_fmt->video_codec != AV_CODEC_ID_NONE)) {
        addStream(&m_outStream[toType(streamType::video)], m_oc, &m_codec[toType(streamType::video)],
                  m_forceCodec.size() > 1 ? m_codec[toType(streamType::video)]->id : m_fmt->video_codec);
        LOG << " using codec: " << m_codec[toType(streamType::video)]->name;

        m_have[toType(streamType::video)] = 1;
        m_encode_video = 1;
    } else {
        LOGE << "no video codec found";
    }

    if (!m_noAudio && m_fmt->audio_codec != AV_CODEC_ID_NONE) {
        addStream(&m_outStream[toType(streamType::audio)], m_oc, &m_codec[toType(streamType::audio)], m_fmt->audio_codec);
        m_have[toType(streamType::audio)] = 1;
        m_encode_audio = 1;
    } else if(!m_noAudio) {
        LOGE << "no audio codec found";
    }

    // Now that all the parameters are set, we can open the audio and
    // video codecs and allocate the necessary encode buffers.
    if (m_have[toType(streamType::video)]) {
        openVideo(m_codec[toType(streamType::video)], &m_outStream[toType(streamType::video)], m_opt);
    }

    if (m_have[toType(streamType::audio)]) {
        openAudio(m_codec[toType(streamType::audio)], &m_outStream[toType(streamType::audio)], m_opt);
    }

    av_dump_format(m_oc, 0, m_par.filePath.c_str(), 1);

    // open the output file, if needed
    if (!(m_fmt->flags & AVFMT_NOFILE)) {
        m_ret = avio_open(&m_oc->pb, m_par.filePath.c_str(), AVIO_FLAG_WRITE);
        if (m_ret < 0) {
            // LOGE << "Could not open '" << m_par.filePath << "': " << ffmpeg::err2str(m_ret);
            return false;
        }
    }

    // Write the stream header, if any. Note: This might change the streams time_base
    m_ret = avformat_write_header(m_oc, &avioOpts);
    if (m_ret < 0) LOGE << "Error occurred when opening output file: " << ffmpeg::err2str(m_ret);

    //ffmpeg::dumpDict(avioOpts, true); // error checking, dictionary must be empty

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

    m_Thread = new std::thread(&FFMpegEncode::recThread, this);
    m_Thread->detach();

    return true;
}

void FFMpegEncode::recThread() {
    m_recCond.notify();

    while (m_doRec) {
        if (m_noAudio) {
            if (m_videoFrames.getFillAmt() > 0) {
                m_encode_video = writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
            } else {
                this_thread::sleep_for(200us);
            }
        } else {
            // select the stream to encode if video is before audio, write video, otherwise audio
            if (av_compare_ts(m_outStream[toType(streamType::video)].next_pts, m_outStream[toType(streamType::video)].enc->time_base,
                              m_outStream[toType(streamType::audio)].next_pts, m_outStream[toType(streamType::audio)].enc->time_base) <= 0) {
                m_encode_video = writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
            } else {
                m_encode_audio = writeAudioFrame(m_oc, &m_outStream[toType(streamType::audio)], false);
            }
        }
    }

    std::cout << "FFMpegEncode stop, remaining video frames: " << m_videoFrames.getFillAmt() << std::endl;

    // save rest of pics m_buffer
    while (m_videoFrames.getFillAmt() > 0) {
        writeVideoFrame(m_oc, &m_outStream[toType(streamType::video)]);
    }

    // save rest of audio m_buffer
    while (m_audioQueue.size() > 0) {
        writeAudioFrame(m_oc, &m_outStream[toType(streamType::audio)], true);
    }

    // Write the trailer, if any. The trailer must be written before you
    // close the CodecContexts open when you wrote the header; otherwise
    // av_codec_close().
    av_write_trailer(m_oc);

    for (int i = 0; i < toType(streamType::size); ++i) {
        if (m_have[i]) {
            closeStream(m_oc, &m_outStream[i]);
        }
    }

    // Close the output file.
    if (!(m_fmt->flags & AVFMT_NOFILE)) {
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
    for (auto &it : m_codec) {
        it = nullptr;
    }
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

void FFMpegEncode::addStream(OutputStream *ost, AVFormatContext *oc, const AVCodec **codec, enum AVCodecID codec_id) {
    // find the encoder
    if (!(*codec)) {
        *codec = (AVCodec*) avcodec_find_encoder(codec_id);
        if (!(*codec)) {
            LOGE << "Could not find encoder for " << avcodec_get_name(codec_id);
            return;
        }
    }

    if (oc) {
        ost->st = avformat_new_stream(oc, nullptr);
        if (!ost->st) {
            LOGE << "Could not allocate stream";
            return;
        }

        ost->st->id = oc->nb_streams -1;
    }

    auto c = avcodec_alloc_context3(*codec);
    if (!c) {
        LOGE << "Could not alloc an encoding context";
        return;
    }

    ost->enc = c;

    switch ((*codec)->type) {
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

    case AVMEDIA_TYPE_VIDEO: {
        c->codec_id = codec_id;
        c->bit_rate = m_videoBitRate;

        // Resolution must be a multiple of two.
        c->width = m_par.width;
        c->height = m_par.height;

        c->framerate = AVRational{m_par.fps, 1};
        c->gop_size = 1; // emit one intra frame every twelve frames at most
        c->pix_fmt = m_par.useHwAccel ? m_hwPixFmt : AV_PIX_FMT_YUV420P;
        c->thread_count = m_par.useHwAccel ? 1 : 4;

        if (m_par.useHwAccel) {
            if ((m_ret = setHwframeCtx(c, m_hw_device_ctx)) < 0) {
                LOGE << "Failed to set hwframe context.";
                return;
            }
        }

        if (c->codec_id == AV_CODEC_ID_H264 || c->codec_id == AV_CODEC_ID_MPEG2TS) {
            //film, animation, grain, stillimage, psnr, ssim, fastdecode, zerolatency
            if (m_is_net_stream) {
                //av_opt_set(c->priv_data, "preset", "superfast", 0);
                av_opt_set(c->priv_data, "vbr", "1", AV_OPT_SEARCH_CHILDREN);
            } else {
                //av_opt_set(c->priv_data, "tune", "animation", 0);
                // ultrafast,superfast, veryfast, faster, fast, medium, slow, slower, veryslow
                // je schneller, desto groesser die dateien
                //av_opt_set(c->priv_data, "preset", "faster", 0);
            }

            // for compatibility
            c->profile = FF_PROFILE_H264_BASELINE;
            c->me_cmp = FF_CMP_CHROMA;
            //	c->me_method = ME_EPZS;
        }

        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            // just for testing, we also add B-frames
            c->max_b_frames = 2;
        }

        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            // Needed to avoid using macroblocks in which some coeffs overflow.
            // This does not happen with normal video, it just happens here as
            // the motion of the chroma plane does not match the luma plane.
            c->mb_decision = 2;
        }

        //if (m_isRtmp)
        //{
            bool twopass = false;
            const char *preset = "superfast"; // "mq";
            const char *profile = "high";
            const char *rc = m_isRtmp ? "VBR" : "CBR";
            int cqp = 20;

            av_opt_set_int(c->priv_data, "cbr", false, 0);
            av_opt_set(c->priv_data, "profile", profile, 0);
            //av_opt_set(c->priv_data, "preset", preset, 0);

            if (strcmp(rc, "cqp") == 0) {
                m_videoBitRate = 0;
                c->global_quality = cqp;

            } else if (strcmp(rc, "lossless") == 0) {
                m_videoBitRate = 0;
                cqp = 0;

                bool hp = (strcmp(preset, "hp") == 0 || strcmp(preset, "llhp") == 0);
                av_opt_set(c->priv_data, "preset", hp ? "losslesshp" : "lossless", 0);

            } else if (strcmp(rc, "vbr") != 0) { // CBR by default
                av_opt_set_int(c->priv_data, "cbr", false, 0);
                av_opt_set_int(c->priv_data, "vbr", true, 0);
                const int64_t rate = m_videoBitRate;
                c->rc_max_rate = rate;
                c->rc_min_rate = rate;
                cqp = 0;
            }

            //av_opt_set(c->priv_data, "level", "auto", 0);
            av_opt_set_int(c->priv_data, "2pass", twopass, 0);
            //av_opt_set_int(c->priv_data, "gpu", gpu, 0);
            //set_psycho_aq(enc, psycho_aq);

            const int rate = m_videoBitRate;
            c->bit_rate = rate;
            c->rc_buffer_size = rate;
            //c->max_b_frames = 2;
            c->gop_size = m_isRtmp ? 25 : 300; // obs setting gop -> group of pictures
            // c->gop_size = 250; // obs setting gop -> group of pictures
            c->time_base = AVRational { 1, m_par.fps };

            /*
            switch (info.colorspace) {
                case VIDEO_CS_601:
                    c->color_trc = AVCOL_TRC_SMPTE170M;
                    c->color_primaries = AVCOL_PRI_SMPTE170M;
                    c->colorspace = AVCOL_SPC_SMPTE170M;
                    break;
                case VIDEO_CS_DEFAULT:
                case VIDEO_CS_709:
                    c->color_trc = AVCOL_TRC_BT709;
                    c->color_primaries = AVCOL_PRI_BT709;
                    c->colorspace = AVCOL_SPC_BT709;
                    break;
                case VIDEO_CS_SRGB:
                    c->color_trc = AVCOL_TRC_IEC61966_2_1;
                    c->color_primaries = AVCOL_PRI_BT709;
                    c->colorspace = AVCOL_SPC_BT709;
                    break;
            }*/

            printf("settings:\n"
                   "\trate_control: %s\n"
                   "\tbitrate:      %d\n"
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
                   rc, m_videoBitRate, cqp, c->gop_size, preset, profile,
                   c->width, c->height,
                   twopass ? "true" : "false", c->max_b_frames
                    //psycho_aq,
                    //gpu
            );

            if (m_ret < 0)
                LOGE << "Error could not set rtmp option";

            // timebase: This is the fundamental unit of time (in seconds) in terms
            // of which frame timestamps are represented. For fixed-fps content,
            // timebase should be 1/framerate and timestamp increments should be
            // identical to 1.

            // tbr
            ost->st->avg_frame_rate = AVRational{m_par.fps, 1}; // needed e.g. for rtmp streams
            ost->st->r_frame_rate = AVRational{m_par.fps, 1}; // tbr,  needed e.g. for rtmp streams
            //ost->st->r_frame_rate = AVRational{1, 1000}; // needed e.g. for rtmp streams

            if (m_isRtmp)
            {
                ost->st->time_base = AVRational{ 1, 1000 }; // tbn
            } else {
                ost->st->time_base = AVRational{ 1, 512 * m_par.fps }; // tbn : 10 -> 10240, 100 -> 12800, 1000 -> 16000, 10000 -> 10000
                //ost->st->time_base = m_par.useHwAccel ? AVRational{1, m_par.fps} : AVRational{1, 1000};
            }

            if (m_isRtmp)
            {
                ost->st->codecpar->extradata = c->extradata;
                ost->st->codecpar->extradata_size = c->extradata_size;
                if (m_ret < 0)
                    LOGE << "Error could not set rtmp option";
            }

            if (ost->st) {
                //m_timeBaseMult = (double) ost->st->time_base.den / (double) ost->st->time_base.num * 0.001; // for converting elapsed encoding time in ms to pts
            }

        //}
    }
        break;
    default:
        break;
    }

    // Some formats want stream headers to be separate.
    if (oc && (oc->oformat->flags & AVFMT_GLOBALHEADER)) {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}

// audio output

void FFMpegEncode::openAudio(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg) {
    AVCodecContext *c;
    int nb_samples;
    AVDictionary *opt = NULL;
    c = ost->enc;

    // open it
    av_dict_copy(&opt, opt_arg, 0);
    auto ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        //LOGE << "Could not open audio codec: " << ffmpeg::err2str(ret);
        return;
    }

    m_src_nb_samples = (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) ? 10000 :c->frame_size;
    nb_samples = m_src_nb_samples;

    ret = av_samples_alloc_array_and_samples(&m_src_samples_data, &m_src_samples_linesize, c->channels,
                                             m_src_nb_samples, c->sample_fmt, 0);
    if (ret < 0) {
        LOGE << "Could not allocate source samples";
        return;
    }

    ost->frame     = allocAudioFrame(c->sample_fmt, c->channel_layout, c->sample_rate, nb_samples);
    ost->tmp_frame = allocAudioFrame(AV_SAMPLE_FMT_S16, c->channel_layout, c->sample_rate, nb_samples);

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
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

    // initialize the resampling context
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        LOGE << "Failed to initialize the resampling context";
        return;
    }

    // compute the number of converted samples: buffering is avoided
    // ensuring that the output m_buffer will contain at least all the
    // converted input samples
    m_max_dst_nb_samples = m_src_nb_samples;
    ret = av_samples_alloc_array_and_samples(&m_dst_samples_data, &m_dst_samples_linesize, c->channels,
                                             m_max_dst_nb_samples, c->sample_fmt, 0);
    if (ret < 0) {
        LOGE << "Could not allocate destination samples";
        return;
    }

    m_dst_samples_size = av_samples_get_buffer_size(nullptr, c->channels, m_max_dst_nb_samples, c->sample_fmt, 0);
}

AVFrame* FFMpegEncode::getAudioFrame(OutputStream *ost, bool clear) {
    AVFrame *frame = ost->tmp_frame;
    int16_t *q = (int16_t*)frame->data[0];
    int newSize = 0;

    if (static_cast<int32_t>(m_audioQueue.size()) >= frame->nb_samples * ost->enc->channels) {
        for (int j=0;j<frame->nb_samples;j++) {
            for (int chanNr = 0; chanNr < ost->enc->channels; chanNr++) {
                *q++ = static_cast<int>(m_audioQueue[j * ost->enc->channels + chanNr] * 32767);
            }
        }

        newSize = static_cast<int32_t>(m_audioQueue.size()) - frame->nb_samples * ost->enc->channels;
        m_audioQueue.erase(m_audioQueue.begin(), m_audioQueue.begin() + frame->nb_samples * ost->enc->channels);
        m_audioQueue.resize(newSize);

    } else if (clear) {
        for (unsigned int j=0; j<(m_audioQueue.size() / ost->enc->channels); j++) {
            for (int chanNr = 0; chanNr < ost->enc->channels; chanNr++) {
                *q++ = static_cast<int>(m_audioQueue[j * ost->enc->channels + chanNr] * 32767);
            }
        }

        // add zeros to complete the frame_size
        for (unsigned int j=0; j<frame->nb_samples - (m_audioQueue.size() / ost->enc->channels); j++) {
            for (int chanNr = 0; chanNr < ost->enc->channels; chanNr++) {
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

    if (m_audioQueue.size() > 0) {
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

            ret = writeFrame(oc, &c->time_base, ost->st, pkt);
            if (ret < 0) {
                //LOGE << "Error while writing audio frame: " << ffmpeg::err2str(ret);
                return -1;
            }

        }
    }

    return (frame || got_packet) ? 0 : 1;
}

// video output

void FFMpegEncode::openVideo(const AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg) {
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = nullptr;
    m_av_interleaved_wrote_first = false;

    av_dict_copy(&opt, opt_arg, 0);

    // open the codec
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        LOGE << "Could not open video codec: " << ffmpeg::err2str(ret);
        return;
    }

    // allocate and init a re-usable frame
    ost->frame = allocPicture(m_par.useHwAccel ? m_hwSwFmt : c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        LOGE << "Could not allocate video frame";
        return;
    }

    // allocate a frame for filtering
    if (m_useFiltering) {
        m_filt_frame = allocPicture(c->pix_fmt, c->width, c->height);
    }

    // copy the stream parameters to the muxer
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        LOGE << "Could not copy the stream parameters";
        return;
    }

    // Allocate an AVFrame structure
    m_inpFrame = allocPicture(m_par.pixelFormat, m_par.width, m_par.height);
    m_frameBGRA = allocPicture(AV_PIX_FMT_BGRA, m_par.width, m_par.height);

    if (m_par.useHwAccel) {
        if (!(m_hw_frame = av_frame_alloc())) {
            m_ret = AVERROR(ENOMEM);
            LOGE << "Couldn't allocate hw frame. No memory left";
            return;
        }
        if ((m_ret = av_hwframe_get_buffer(c->hw_frames_ctx, m_hw_frame, 0)) < 0) {
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
    m_encCodecCtx = ost->enc;
    m_encFrame = ost->frame;

    if (!m_encFrame) {
        return 0;
    }

    // only write if there was a frame downloaded
    if (m_useFiltering) {
        // allocate a frame with the settings of the context
        m_encFrame = av_frame_alloc();
        if (!m_encFrame) LOGE << "error copying frame to m_encFrame";

        av_image_alloc(m_encFrame->data, m_encFrame->linesize, m_encCodecCtx->width, m_encCodecCtx->height,
                       m_encCodecCtx->pix_fmt, 1);
        m_encFrame->width = m_encCodecCtx->width;
        m_encFrame->height = m_encCodecCtx->height;

        // get picture from the m_buffer
        // reset picture type for having encoder setting it
        m_encFrame->pict_type = AV_PICTURE_TYPE_NONE;
        m_encFrame->format = m_encCodecCtx->pix_fmt;
    }

    // check for the frame closest to the next pts
    /*if (m_par.useHwAccel)
    {
        double nextPts = (double)ost->next_pts * (double)ost->enc->time_base.num / (double)ost->enc->time_base.den * 1000.0;

        map<double, int> sortRecQueue;
        for (unsigned int i=0; i<m_nrBufferFrames; i++)
            if (m_videoFrames[i].encTime >= 0)
                sortRecQueue[std::abs(m_videoFrames[i].encTime - nextPts)] = i;

        if (sortRecQueue.empty()) {
            LOGE << "sortRecQueue.empty()";
            this_thread::sleep_for(500us);
            return -1;
        }
    } */

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
    m_videoFrames.consumeCountUp();

    // doing screencast from a display with 60fps results in jittering playback, since the monitors framerate is not exactly constant
    // using libx264 with a high resolution time-base and directly using the elapsed time as pts gives the best results (e.m_g. monitor 60fps to 30fps encoding)
    // unfortunately with NVENC this doesn't work...

    if (m_par.useHwAccel) {
        ost->frame->pts = ost->next_pts++;  // fixed frameduration
        m_hw_frame->pts = ost->frame->pts;
    } else {
        ost->frame->pts = ost->next_pts++;  // fixed frameduration
    }

    auto encPixFmt = m_par.useHwAccel ? m_hwSwFmt : m_encCodecCtx->pix_fmt;

    // flip image horizontally
    if (m_flipH) {
        // setup a sws context. this checks if the context can be reused
        m_converter = sws_getCachedContext(m_converter,
                                            m_par.width,	            // srcwidth
                                            m_par.height,	            // srcheight
                                            m_par.pixelFormat,		        // src_format
                                            m_encCodecCtx->width,	// dstWidth
                                            m_encCodecCtx->height,	// dstHeight
                                            encPixFmt,	// dst_format
                                            m_sws_flags,			// flags
                                            nullptr,			// srcFilter
                                            nullptr,		// dstFilter
                                            nullptr			// param
                                            );

        *m_inpFrame->data = *m_inpFrame->data + m_par.width * m_glNrBytesPerPixel * (m_par.height - 1);
        for (int i=0;i<8;i++) {
            m_stride[i] = -m_inpFrame->linesize[i];
        }
    } else {
        memcpy(m_stride, m_inpFrame->linesize, 8);
    }

    if (m_useFiltering) {
        sws_scale(m_converter, m_inpFrame->data, m_stride, 1,
                  m_encCodecCtx->height, ost->frame->data, ost->frame->linesize);

        // push the frame into the filtergraph
        // killt den frame
        m_ret = av_buffersrc_add_frame(m_buffersrc_ctx, m_encFrame);
        if (m_ret != 0) {
            std::cout << "Error while feeding the filtergraph. error code " << m_ret << std::endl;
        }

        if (av_buffersink_get_frame(m_buffersink_ctx, m_filt_frame) < 0) {
            std::cout << "error filtering frame" << std::endl;
        }

        m_filt_frame->pts = m_inpFrame->pts;
    } else {
        // combination of libyuv::ARGBToNV12 take around 2 ms on an i7-7700HQ CPU @ 2.80GHz for an HD image
        // in comparison to 7 ms with sws_scale

        // Note: libyuv always take planes in reverse order: ffmpeg BGRA corresponds to libyuv ARGB
        if (encPixFmt == AV_PIX_FMT_NV12) {
            AVFrame* inFrame = m_inpFrame;

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
            return -1;
        }
        m_encFrame = m_hw_frame;
    }

     av_init_packet(&m_decPkt);

    if (!m_par.useHwAccel) {
        m_ret = av_frame_make_writable(ost->frame);
    }

    // send the frame to the encoder
    auto frame = m_useFiltering ? m_filt_frame : m_encFrame;
    m_ret = avcodec_send_frame(m_encCodecCtx, frame);
    if (m_ret < 0) {
        LOGE <<  "Error sending a frame for encoding";
        av_packet_unref(&m_decPkt);
        return 0;
    }

    while (m_ret >= 0) {
        m_ret = avcodec_receive_packet(m_encCodecCtx, &m_decPkt);
        if (m_ret == AVERROR(EAGAIN) || m_ret == AVERROR_EOF) {
            break;
        } else if (m_ret < 0) {
            LOGE <<  "Error during encoding";
            break;
        }

        m_decPkt.duration = (int64_t) (1.0 / static_cast<double>(m_par.fps) * 1000.0);
        m_ret = writeFrame(oc, &ost->enc->time_base, ost->st, &m_decPkt); // paket is freed implicitly
    }

    return m_encFrame ? 0 : 1;
}

int FFMpegEncode::writeFrame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt) {
    // rescale output packet timestamp values from codec to stream timebase
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    int r=0;

    // Write the compressed frame to the media file.
    // NOTE: in case of network streams av_interleaved_write_frame will wait until the packet reached and thus contain the network delay

    //logPacket(fmt_ctx, pkt);

    r = av_interleaved_write_frame(fmt_ctx, pkt);
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
void FFMpegEncode::downloadGlFbToVideoFrame(double fixFps, unsigned char* bufPtr) {
    if (!m_doRec) {
        return;
    }

    //double fixFrameDur = fixFps != 0.0 ? (1000.0 / fixFps) : 1.0;
   // unique_lock<mutex> l(m_writeMtx);

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
    if ((m_par.useHwAccel && fixFps != 0.0) && m_encTimeDiff < m_frameDur * 0.75){
        return;
    }

    if (fixFps == 0.0 && (m_frameDur - m_encTimeDiff) > (m_frameDur * 0.5)){
        return;
    }

    // skip frame if queue full
    if (m_videoFrames.isFilled()) {
        LOGE << "FFMpegEncode::downloadGlFbToVideoFrame(): buffer queue full, skipping frame";
        if (m_bufOvrCb) {
            m_lastBufOvrTime = chrono::system_clock::now();
            m_bufOvr = true;
            m_bufOvrCb(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // skip frame and wait
        return;
    } else {
        if (m_bufOvrCb) {
            m_encElapsedTime = std::chrono::duration<double, std::milli>(chrono::system_clock::now() - m_lastBufOvrTime).count();
            if (m_bufOvr && m_encElapsedTime > 1000){
                m_bufOvr = false;
                m_bufOvrCb(false);
            }
        }
    }

    bool gotData = false;
    if (!bufPtr) {
        if (m_pbos.getFillAmt() > 0 && !m_videoFrames.isFilled()) {
            LOG << "read buf " << m_pbos.getReadBuff();
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos.getReadBuff());
#ifdef ARA_USE_GLES31
            auto ptr = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, m_nbytes, GL_MAP_READ_BIT));
#else
            auto ptr = static_cast<uint8_t *>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
#endif
            if (ptr) {
                LOG << ptr;
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
            LOG << "write buf " << m_pbos.getWriteBuff();
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos.getWriteBuff());
            glReadPixels(0, 0, m_par.width, m_par.height, m_glDownloadFmt, GL_UNSIGNED_BYTE, 0);
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
        if (filesystem::exists(m_downloadFolder)) {
            filesystem::remove_all(m_downloadFolder);
        }

        if (!filesystem::exists(m_downloadFolder)) {
            filesystem::create_directory(m_downloadFolder);
        }

        m_saveThread = std::thread([this]{
            while (m_doRec){
                if (m_saveQueueSize > 0){
                    m_saveQueue[m_saveQueueRead]();
                    ++m_saveQueueRead %= m_saveQueue.size();
                    m_saveQueueSize--;
                } else {
                    this_thread::sleep_for(400us);
                }
            }
        });
        m_saveThread.detach();
        m_savePngFirstCall = false;
    }

    if (!m_downTex) {
        m_downTex = make_unique<Texture>();
    }

    if (m_videoFrames.getReadBuff().bufferPtr) {
        auto bPtr = m_videoFrames.getReadBuff().bufferPtr;
        auto seqPtr = m_pngSeqCnt;
        m_saveQueue[m_saveQueueWrite] = [bPtr, seqPtr, this] {
            m_downTex->saveBufToFile2D((m_downloadFolder.string()+"/seq_"
                                        +std::to_string(seqPtr / 1000 % 10)
                                        +std::to_string(seqPtr / 100 % 10)
                                        +std::to_string(seqPtr / 10 % 10)
                                        +std::to_string(seqPtr % 10)+".tga").c_str(),
                                       FIF_TARGA,
                                       m_par.width,
                                       m_par.height,
                                       4,
                                       bPtr);
        };

        ++m_saveQueueWrite %= m_saveQueue.size();
        m_saveQueueSize++;
        if (m_saveQueueSize >= m_saveQueue.size()) {
            LOGE << " save Queue Overflow!";
        }
        m_pngSeqCnt++;
        m_videoFrames.consumeCountUp();
    }
}

#ifdef WITH_AUDIO

void FFMpegEncode::mediaRecAudioDataCallback(PAudio::paPostProcData* paData)
{
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