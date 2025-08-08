//
// Created by user on 11.08.2021.
//

#pragma once
#ifdef ARA_USE_FFMPEG

#ifdef ARA_USE_GLBASE
#include <GlbCommon/GlbCommon.h>
#include <Utils/Texture.h>
#include <Shaders/ShaderCollector.h>
#endif
#include <Conditional.h>

#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavcodec/bsf.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avassert.h>
#include <libavutil/hwcontext.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h>
#include <libavutil/fifo.h>
#include <libavutil/parseutils.h>
#include <libavutil/avstring.h>
#include <libavutil/intreadwrite.h>

#ifndef AV_PKT_FLAG_KEY
#define AV_PKT_FLAG_KEY PKT_FLAG_KEY
#endif
}

#ifdef  __linux__
#ifdef  __cplusplus

#ifndef AV_MAKE_ERROR_STRING_IMPL
#define AV_MAKE_ERROR_STRING_IMPL
static std::string av_make_error_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return (std::string)errbuf;
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string(errnum).c_str()

#endif

#endif // __cplusplus
#endif // __linux__

namespace ara {
    class GLBase;
}

namespace ara::av::ffmpeg {

struct TimedFrame {
    AVFrame* frame=nullptr;
    double ptss=0;
};

struct TimedPicture : public TimedFrame {
    std::vector<uint8_t> buf;
};

struct DecodePar {
    GLBase*     glbase = nullptr;
    std::string filePath;
    int         useNrThreads = 1;
    int         destWidth = 0;
    int         destHeight = 0;
    bool        useHwAccel = true;
    bool        decodeYuv420OnGpu = true;
    bool        startDecodeThread = false;
    std::string assetName;

    std::function<void()> initCb;
};

static std::string m_errStr;
static std::string m_tStr;
static char m_tmChar[AV_TS_MAX_STRING_SIZE];
static int m_logLevel=AV_LOG_INFO;

struct memin_buffer_data {
    uint8_t *ptr=nullptr;
    size_t size=0; ///< size left in the buffer
    uint8_t *start=nullptr;
    size_t fileSize=0;
};

static void get_packet_defaults(AVPacket *pkt) {
    memset(pkt, 0, sizeof(*pkt));
    pkt->pts = AV_NOPTS_VALUE;
    pkt->dts = AV_NOPTS_VALUE;
    pkt->pos = -1;
}

static inline std::string& err2str(int errnum) {
    if (m_errStr.size() < AV_ERROR_MAX_STRING_SIZE) {
        m_errStr.resize(AV_ERROR_MAX_STRING_SIZE);
    }
    av_make_error_string(&m_errStr[0], AV_ERROR_MAX_STRING_SIZE, errnum);
    return m_errStr;
}

static inline char* ts_make_string(char *buf, int64_t ts) {
    if (ts == AV_NOPTS_VALUE) {
        snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    } else {
        snprintf(buf, AV_TS_MAX_STRING_SIZE, "%" PRId64, ts);
    }
    return buf;
}

static inline char* ts_make_time_string(char *buf, int64_t ts, AVRational *tb) {
    if (ts == AV_NOPTS_VALUE) {
        snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    } else {
        snprintf(buf, AV_TS_MAX_STRING_SIZE, "%.6g", av_q2d(*tb) * ts);
    }
    return buf;
}

static inline char* ts2str(int64_t ts) {
    ts_make_string(m_tmChar, ts);
    return m_tmChar;
}

static inline char* ts2timestr(int64_t ts, AVRational* tb) {
    ts_make_time_string(m_tmChar, ts, tb);
    return m_tmChar;
}

static inline void LogCallbackShim(void *ptr, int level, const char *fmt, va_list vargs) {
    char buffer[1024];
    vsprintf (buffer,fmt, vargs);
    if (level <= m_logLevel)
#ifdef __ANDROID__
        LOGN << buffer;
#else
        std::cout << buffer;
#endif
}

static inline void dumpDict(AVDictionary* dict, bool printAsError=false) {
    if (av_dict_count(dict) > 0) {
        AVDictionaryEntry *entry = nullptr;
        if (printAsError) {
            LOGE << "AVDictionary [" << dict << "] dump: ";
        } else {
            LOG << "AVDictionary [" << dict << "] dump: ";
        }

        int i=0;
        while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            if (printAsError) {
                LOGE << "\t [" << i << "] \"" << entry->key << "\": " <<  entry->value;
            } else {
                LOG << "\t [" << i << "] \"" << entry->key << "\": " <<  entry->value;
            }
            ++i;
        }
    }
}

static enum AVPixelFormat findFmtByHwType(const enum AVHWDeviceType type) {
    std::unordered_map<enum AVHWDeviceType, AVPixelFormat> fmtMap{
        { AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI },
        { AV_HWDEVICE_TYPE_DXVA2, AV_PIX_FMT_DXVA2_VLD },
        { AV_HWDEVICE_TYPE_D3D11VA, AV_PIX_FMT_D3D11 },
        { AV_HWDEVICE_TYPE_VDPAU, AV_PIX_FMT_VDPAU },
        { AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV },
        { AV_HWDEVICE_TYPE_VIDEOTOOLBOX, AV_PIX_FMT_VIDEOTOOLBOX },
        { AV_HWDEVICE_TYPE_MEDIACODEC, AV_PIX_FMT_MEDIACODEC }
    };
    return fmtMap.find(type) != fmtMap.end() ? fmtMap[type] : AV_PIX_FMT_NONE;
}

static inline AVPixelFormat hwFormatToCheck{};

static enum AVPixelFormat getHwFormat(AVCodecContext*, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    enum AVPixelFormat ret={AVPixelFormat(0)};
    bool gotFirst=false;
    bool found=false;

    for (p = pix_fmts; *p != -1; p++) {
        if (!gotFirst){
            ret = *p;
            gotFirst = true;
        }
        if (*p == hwFormatToCheck) {
            ret = *p;
            found = true;
            break;
        }
    }

    if (!found) {
        LOG << "FFMpegDecode Warning: Didn't find requested HW format. Took default instead";
    }

    return ret;
}

static const AVCodec* getCodecFromId(const AVCodecID& id) {
    auto localCodec = avcodec_find_decoder(id);
    if (localCodec) {
        if (localCodec->pix_fmts && localCodec->pix_fmts[0] != -1) {
            int ind = 0;
            while (localCodec->pix_fmts[ind] != -1) {
                LOG << "CODEC possible pix_fmts: " << localCodec->pix_fmts[ind];
                ++ind;
            }
        }
    }

    return localCodec;
}

static void dumpEncoders() {
    const AVCodec *current_codec = nullptr;
    void *i{};
    while ((current_codec = av_codec_iterate(&i))) {
        if (av_codec_is_encoder(current_codec)) {
            LOG <<  current_codec->name << " " << current_codec->long_name;
        }
    }
}

static void dumpDecoders() {
    const AVCodec *current_codec = nullptr;
    void *i{};
    while ((current_codec = av_codec_iterate(&i))) {
        if (av_codec_is_decoder(current_codec)) {
            LOG <<  current_codec->name << " " << (current_codec->long_name ? current_codec->long_name : "");
        }
    }
}

static GLenum getGlColorFormatFromAVPixelFormat(AVPixelFormat srcFmt) {
    std::unordered_map<AVPixelFormat, int> formatMap {
            {AV_PIX_FMT_YUV420P, GL_RED},
            {AV_PIX_FMT_NV12, GL_RED},
            {AV_PIX_FMT_NV21, GL_RED},
            {AV_PIX_FMT_RGB24, GL_BGR},
    };
    return formatMap[srcFmt];
}

static double r2d(AVRational r) {
    return r.num == 0 || r.den == 0 ? 0. : (double) r.num / (double) r.den;
}

static int checkStreamSpecifier(AVFormatContext *s, AVStream *st, const char *spec) {
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    }
    return ret;
}

static AVDictionary* filterCodecOpts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s,
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

static AVDictionary** setupFindStreamInfoOpts(AVFormatContext *s, AVDictionary *codec_opts) {
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

}
#endif