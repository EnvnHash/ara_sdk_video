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
#include "get_bits.h"

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

struct DecodePar {
    GLBase*     glbase = nullptr;
    std::string filePath;
    int         useNrThreads = 1;
    int         destWidth = 0;
    int         destHeight = 0;
    bool        useHwAccel = true;
    bool        decodeYuv420OnGpu = true;
    bool        doStart = false;
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

}
#endif