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
#ifdef ARA_USE_GLBASE
    if (p.glbase){
        m_shCol = &p.glbase->shaderCollector();
    }
#endif
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

#ifdef ARA_USE_GLBASE
    if (m_par.glbase) {
        m_shCol = &m_par.glbase->shaderCollector();
    }
#endif

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
        if (m_videoCodecName) {
            throw runtime_error("No codec could be found with name "+std::string(m_videoCodecName));
        } else {
            throw runtime_error("No decoder could be found for codec "+std::string(avcodec_get_name(m_videoCodecCtx->codec_id)));
        }
    }

    m_videoCodecCtx->pkt_timebase = m_formatContext->streams[i]->time_base;
    m_videoCodecCtx->codec_id = video_codec->id;

    if (m_par.useHwAccel && !m_useMediaCodec) {
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
    if (m_forceSampleRate != 0) {
        localCodecParameters->sample_rate = m_forceSampleRate;
    }

    if (m_forceNumChannels != 0) {
        localCodecParameters->channels = m_forceNumChannels;
    }

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

    if (m_videoCodecCtx) {
#ifdef ARA_USE_GLBASE
        if (!p.decodeYuv420OnGpu && p.destWidth && p.destHeight) {
            m_buffer = std::vector<std::vector<uint8_t>>(m_videoFrameBufferSize);
            m_bgraFrame = std::vector<AVFrame*>(m_videoFrameBufferSize);
            for (uint32_t i = 0; i < m_videoFrameBufferSize; i++) {
                m_bgraFrame[i] = allocPicture(m_destPixFmt, p.destWidth, p.destHeight, m_buffer.begin() + i);
            }
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

    m_totNumFrames = static_cast<uint>(getTotalFrames());
    m_resourcesAllocated = true;
}

AVDictionary** FFMpegDecode::setupFindStreamInfoOpts(AVFormatContext *s, AVDictionary *codec_opts) {
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
            LOGE << "FFMpegDecode::setupFindStreamInfoOpts Error streams[i]->codecpar == null";
            continue;
        }
        opts[i] = filterCodecOpts(codec_opts, s->streams[i]->codecpar->codec_id, s, s->streams[i], nullptr);
    }

    return opts;
}

AVDictionary* FFMpegDecode::filterCodecOpts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s,
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
            switch (checkStreamSpecifier(s, st, p + 1)) {
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

int FFMpegDecode::checkStreamSpecifier(AVFormatContext *s, AVStream *st, const char *spec) {
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    }
    return ret;
}

#ifdef ARA_USE_GLBASE
void FFMpegDecode::initShader(AVPixelFormat srcPixFmt, ffmpeg::DecodePar& p) {
    if (p.decodeYuv420OnGpu && !m_shCol->hasShader("FFMpegDecode_yuv")) {
        auto shdr_Header = ara::ShaderCollector::getShaderHeader();

        std::string vert = STRINGIFY( layout(location = 0) in vec4 position;    \n
            layout(location = 2) in vec2 texCoord;                              \n
            uniform mat4 m_pvm;                                                 \n
            out vec2 tex_coord;                                                 \n
            void main() {                                                       \n
                \t tex_coord = texCoord;                                        \n
                \t gl_Position = m_pvm * position;                              \n
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

        if (m_par.decodeYuv420OnGpu) {
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
#endif

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

void FFMpegDecode::allocGlRes(AVPixelFormat srcPixFmt) {
#ifdef ARA_USE_GLBASE
    initShader(srcPixFmt, m_par);

    if (m_par.decodeYuv420OnGpu) {
        m_nrTexBuffers = (srcPixFmt == AV_PIX_FMT_NV12 || srcPixFmt == AV_PIX_FMT_NV21) ? 2 : 3;
    } else {
        m_nrTexBuffers = 1;
    }

    m_textures = std::vector<std::unique_ptr<Texture>>(m_nrTexBuffers);
    for (auto &it : m_textures) {
        it = make_unique<Texture>(m_par.glbase);
    }

    if (m_par.decodeYuv420OnGpu) {
        if (m_srcPixFmt == AV_PIX_FMT_NV12 || m_srcPixFmt == AV_PIX_FMT_NV21) {
            m_textures[0]->allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_RG8, GL_RG, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        } else {   // YUV420P
            m_textures[0]->allocate2D(m_srcWidth, m_srcHeight, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[1]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
            m_textures[2]->allocate2D(m_srcWidth / 2, m_srcHeight / 2, GL_R8, GL_RED, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
        }
    } else {
        m_textures[0]->allocate2D(m_par.destWidth, m_par.destHeight, GL_RGB8, GL_RGB, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
    }
#endif
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

int FFMpegDecode::decodeVideoPacket(AVPacket* packet, AVCodecContext* codecContext) {
    if (!codecContext) {
        return 0;
    }

    int32_t response = -1;

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
            if (!m_useMediaCodec && m_par.useHwAccel) {
                response = avcodec_receive_frame(codecContext, m_frame);            // always calls av_frame_unref
            } else if (m_useMediaCodec && m_par.useHwAccel){
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
                if (!m_par.decodeYuv420OnGpu) {
                    m_mutex.lock();

                    // convert frame to desired size and m_format
                    if (m_par.useHwAccel && !m_useMediaCodec && m_frame->format == m_hwPixFmt) {
                        transferFromHwToCpu();
                    } else if (m_par.useHwAccel && m_useMediaCodec) {
                        transferFromMediacodecToCpu();
                    }

                    // since now for the first time we are really sure about the pix_fmt the decode
                    // frame will have, initialize the textures and the swscale context if necessary
                    if (convertFrameToCpuFormat(codecContext) < 0) {
                        LOGE << "FFMpegDecode ERROR, sws_scale failed!!!";
                    }

                    if (m_decodeCb) {
                        m_decodeCb(m_bgraFrame[m_decFramePtr]->data[0]);
                    }

                    m_mutex.unlock();
                } else {
                    if (m_par.useHwAccel
                        && !m_useMediaCodec
                        && (m_frame->format > -1)
                        && (m_frame->format == m_hwPixFmt)) {
                        m_mutex.lock();
                        transferFromHwToCpu();
                        m_srcPixFmt = static_cast<AVPixelFormat>(m_framePtr[m_decFramePtr]->format);
                        m_mutex.unlock();
                    } else if (m_par.useHwAccel && m_useMediaCodec) {
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
                    m_videoStartPts = (m_par.useHwAccel && !m_useMediaCodec) ? m_frame->pts
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

void FFMpegDecode::transferFromMediacodecToCpu() {
#ifdef __ANDROID__
    size_t hwBufSize;
    auto buffer = mediaCodecGetOutputBuffer(response, hwBufSize);

    // LOG << m_rawBuffer[m_decFramePtr].size();
    memcpy(&m_framePtr[m_decFramePtr]->data[0][0], buffer, m_rawBuffer[m_decFramePtr].size());

    m_framePtr[m_decFramePtr]->pts = m_mediaCodecInfo.presentationTimeUs * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base) * 1000;

    m_framePtr[m_decFramePtr]->pkt_size = packet->size;
    m_framePtr[m_decFramePtr]->format = (AVPixelFormat) codecContext->pix_fmt;
    //m_framePtr[m_decFramePtr]->coded_picture_number = packet->coded_picture_number;
    //m_framePtr[m_decFramePtr]->pict_type = packet->pi;

    mediaCodecReleaseOutputBuffer(response);
#endif
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

#ifdef ARA_USE_GLBASE
void FFMpegDecode::loadFrameToTexture(double time) {
    if (m_resourcesAllocated && m_run && !m_pause && m_nrBufferedFrames >= 1) {
        double actRelTime = time - m_startTime + ((double)m_videoStartPts * m_timeBaseDiv);
        uint searchInd = (m_frameToUpload +1) % m_videoFrameBufferSize;
        bool uploadNewFrame = false;

        if (!m_glResInited && m_srcWidth && m_srcHeight) {
            allocGlRes(m_srcPixFmt);
            m_glResInited = true;
        }

        if (!m_glResInited) {
            return;
        }

        // check for the first frame or a frame with a pts close to the actual time
        if (!m_hasNoTimeStamp) {
            while (searchInd < (uint) m_videoFrameBufferSize) {
                if (!m_firstFramePresented) {
                    if ((m_ptss[searchInd % m_videoFrameBufferSize] == m_videoStartPts) || m_isStream) {
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

            if (m_par.decodeYuv420OnGpu) {
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
                                    getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
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
                                    getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[0]);

                    int chroma_width = m_srcWidth / 2;
                    int chroma_height = m_srcHeight / 2;

                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, m_textures[1]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                                    getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
                                    m_framePtr[m_frameToUpload]->data[1]);


                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, m_textures[2]->getId());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height,
                                    getGlColorFormatFromAVPixelFormat(m_srcPixFmt), GL_UNSIGNED_BYTE,
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
                                    m_par.destWidth,              // width and height
                                    m_par.destHeight,
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
                    glBufferData(GL_PIXEL_UNPACK_BUFFER, m_par.destWidth * m_par.destHeight * 4, nullptr, GL_STREAM_DRAW);

                    // map the buffer object into client's memory
                    auto ptr = (GLubyte *) glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, m_par.destWidth * m_par.destHeight * 4, GL_MAP_WRITE_BIT);

                    if (ptr && !m_framePtr.empty())
                    {
                        // update data directly on the mapped buffer
                        memcpy(ptr, m_bgraFrame[m_frameToUpload]->data[0], m_par.destWidth * m_par.destHeight * 4);
                        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release the mapped buffer
                    }

                    // it is good idea to release PBOs with ID 0 after use.
                    // Once bound with 0, all pixel operations are back to normal ways.
                    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

                } else {
                    if (!m_framePtr.empty()) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, m_textures[0]->getId());
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_par.destWidth, m_par.destHeight,
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
#endif

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

#ifdef ARA_USE_GLBASE
    if (m_usePbos){
        glDeleteBuffers(m_nrPboBufs, &m_pbos[0]);
        m_pbos.clear();
    }
#endif

    m_ptss.clear();

    if (m_hwDeviceCtx){
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    if (m_imgConvertCtx){
        sws_freeContext(m_imgConvertCtx);
        m_imgConvertCtx = nullptr;
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