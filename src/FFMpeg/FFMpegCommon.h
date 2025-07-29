//
// Created by user on 11.08.2021.
//

#pragma once
#ifdef ARA_USE_FFMPEG

#ifdef ARA_USE_LIBRTMP
#include <Network/rtmp/librtmp/rtmp.h>
#endif

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
static const std::string av_make_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return (std::string)errbuf;
}

#undef av_err2str
#define av_err2str(errnum) av_make_error_string(errnum).c_str()

#endif

#endif // __cplusplus
#endif // __linux__

namespace ara::av::ffmpeg
{

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

static void get_packet_defaults(AVPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));

    pkt->pts             = AV_NOPTS_VALUE;
    pkt->dts             = AV_NOPTS_VALUE;
    pkt->pos             = -1;
}

static inline std::string& err2str(int errnum)
{
    if (m_errStr.size() < AV_ERROR_MAX_STRING_SIZE)
        m_errStr.resize(AV_ERROR_MAX_STRING_SIZE);
    av_make_error_string(&m_errStr[0], AV_ERROR_MAX_STRING_SIZE, errnum);
    return m_errStr;
}

static inline char* ts_make_string(char *buf, int64_t ts)
{
    if (ts == AV_NOPTS_VALUE) snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    else                      snprintf(buf, AV_TS_MAX_STRING_SIZE, "%" PRId64, ts);
    return buf;
}

static inline char* ts_make_time_string(char *buf, int64_t ts, AVRational *tb)
{
    if (ts == AV_NOPTS_VALUE) snprintf(buf, AV_TS_MAX_STRING_SIZE, "NOPTS");
    else                      snprintf(buf, AV_TS_MAX_STRING_SIZE, "%.6g", av_q2d(*tb) * ts);
    return buf;
}

static inline char* ts2str(int64_t ts)
{
    ts_make_string(m_tmChar, ts);
    return m_tmChar;
}

static inline char* ts2timestr(int64_t ts, AVRational* tb)
{
    ts_make_time_string(m_tmChar, ts, tb);
    return m_tmChar;
}

static inline void LogCallbackShim(void *ptr, int level, const char *fmt, va_list vargs)
{
    char buffer[1024];
    vsprintf (buffer,fmt, vargs);
    if (level <= m_logLevel)
#ifdef __ANDROID__
        LOGN << buffer;
#else
        std::cout << buffer;
#endif
}

static inline void dumpDict(AVDictionary* dict, bool printAsError=false)
{
    if (av_dict_count(dict) > 0)
    {
        AVDictionaryEntry *entry = nullptr;

        if (printAsError)
            LOGE << "AVDictionary [" << dict << "] dump: ";
        else
            LOG << "AVDictionary [" << dict << "] dump: ";

        int i=0;
        while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX)))
        {
            if (printAsError)
                LOGE << "\t [" << i << "] \"" << entry->key << "\": " <<  entry->value;
            else
                LOG << "\t [" << i << "] \"" << entry->key << "\": " <<  entry->value;
            i++;
        }
    }
}

#ifdef ARA_USE_LIBRTMP

#define FLV_VIDEO_FRAMETYPE_OFFSET   4
#define FLV_AUDIO_SAMPLESSIZE_OFFSET 1
#define FLV_AUDIO_SAMPLERATE_OFFSET  2
#define FLV_AUDIO_CODECID_OFFSET     4
#define AMF_END_OF_OBJECT         0x09

enum FlvTagType {
    FLV_TAG_TYPE_AUDIO = 0x08,
    FLV_TAG_TYPE_VIDEO = 0x09,
    FLV_TAG_TYPE_META  = 0x12,
};

enum {
    FLV_HEADER_FLAG_HASVIDEO = 1,
    FLV_HEADER_FLAG_HASAUDIO = 4,
};

enum {
    FLV_FRAME_KEY            = 1 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< key frame (for AVC, a seekable frame)
    FLV_FRAME_INTER          = 2 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< inter frame (for AVC, a non-seekable frame)
    FLV_FRAME_DISP_INTER     = 3 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< disposable inter frame (H.263 only)
    FLV_FRAME_GENERATED_KEY  = 4 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< generated key frame (reserved for server use only)
    FLV_FRAME_VIDEO_INFO_CMD = 5 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< video info/command frame
};

typedef enum {
    FLV_AAC_SEQ_HEADER_DETECT = (1 << 0),
    FLV_NO_SEQUENCE_END = (1 << 1),
    FLV_ADD_KEYFRAME_INDEX = (1 << 2),
    FLV_NO_METADATA = (1 << 3),
    FLV_NO_DURATION_FILESIZE = (1 << 4),
} FLVFlags;

enum {
    FLV_SAMPLESSIZE_8BIT  = 0,
    FLV_SAMPLESSIZE_16BIT = 1 << FLV_AUDIO_SAMPLESSIZE_OFFSET,
};

enum {
    FLV_SAMPLERATE_SPECIAL = 0, /**< signifies 5512Hz and 8000Hz in the case of NELLYMOSER */
    FLV_SAMPLERATE_11025HZ = 1 << FLV_AUDIO_SAMPLERATE_OFFSET,
    FLV_SAMPLERATE_22050HZ = 2 << FLV_AUDIO_SAMPLERATE_OFFSET,
    FLV_SAMPLERATE_44100HZ = 3 << FLV_AUDIO_SAMPLERATE_OFFSET,
    };

enum {
    FLV_CODECID_PCM                  = 0,
    FLV_CODECID_ADPCM                = 1 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_MP3                  = 2 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_PCM_LE               = 3 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_NELLYMOSER_16KHZ_MONO = 4 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_NELLYMOSER_8KHZ_MONO = 5 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_NELLYMOSER           = 6 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_PCM_ALAW             = 7 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_PCM_MULAW            = 8 << FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_AAC                  = 10<< FLV_AUDIO_CODECID_OFFSET,
    FLV_CODECID_SPEEX                = 11<< FLV_AUDIO_CODECID_OFFSET,
    };

typedef enum {
    AMF_DATA_TYPE_NUMBER      = 0x00,
    AMF_DATA_TYPE_BOOL        = 0x01,
    AMF_DATA_TYPE_STRING      = 0x02,
    AMF_DATA_TYPE_OBJECT      = 0x03,
    AMF_DATA_TYPE_NULL        = 0x05,
    AMF_DATA_TYPE_UNDEFINED   = 0x06,
    AMF_DATA_TYPE_REFERENCE   = 0x07,
    AMF_DATA_TYPE_MIXEDARRAY  = 0x08,
    AMF_DATA_TYPE_OBJECT_END  = 0x09,
    AMF_DATA_TYPE_ARRAY       = 0x0a,
    AMF_DATA_TYPE_DATE        = 0x0b,
    AMF_DATA_TYPE_LONG_STRING = 0x0c,
    AMF_DATA_TYPE_UNSUPPORTED = 0x0d,
} AMFDataType;

enum {
    // 7.4.2.1.1: seq_parameter_set_id is in [0, 31].
    H264_MAX_SPS_COUNT = 32,
    // 7.4.2.2: pic_parameter_set_id is in [0, 255].
    H264_MAX_PPS_COUNT = 256,

    // A.3: MaxDpbFrames is bounded above by 16.
    H264_MAX_DPB_FRAMES = 16,
    // 7.4.2.1.1: max_num_ref_frames is in [0, MaxDpbFrames], and
    // each reference frame can have two fields.
    H264_MAX_REFS       = 2 * H264_MAX_DPB_FRAMES,

    // 7.4.3.1: modification_of_pic_nums_idc is not equal to 3 at most
    // num_ref_idx_lN_active_minus1 + 1 times (that is, once for each
    // possible reference), then equal to 3 once.
    H264_MAX_RPLM_COUNT = H264_MAX_REFS + 1,

    // 7.4.3.3: in the worst case, we begin with a full short-term
    // reference picture list.  Each picture in turn is moved to the
    // long-term list (type 3) and then discarded from there (type 2).
    // Then, we set the length of the long-term list (type 4), mark
    // the current picture as long-term (type 6) and terminate the
    // process (type 0).
    H264_MAX_MMCO_COUNT = H264_MAX_REFS * 2 + 3,

    // A.2.1, A.2.3: profiles supporting FMO constrain
    // num_slice_groups_minus1 to be in [0, 7].
    H264_MAX_SLICE_GROUPS = 8,

    // E.2.2: cpb_cnt_minus1 is in [0, 31].
    H264_MAX_CPB_CNT = 32,

    // A.3: in table A-1 the highest level allows a MaxFS of 139264.
    H264_MAX_MB_PIC_SIZE = 139264,
    // A.3.1, A.3.2: PicWidthInMbs and PicHeightInMbs are constrained
    // to be not greater than sqrt(MaxFS * 8).  Hence height/width are
    // bounded above by sqrt(139264 * 8) = 1055.5 macroblocks.
    H264_MAX_MB_WIDTH    = 1055,
    H264_MAX_MB_HEIGHT   = 1055,
    H264_MAX_WIDTH       = H264_MAX_MB_WIDTH  * 16,
    H264_MAX_HEIGHT      = H264_MAX_MB_HEIGHT * 16,
};

enum { FLV_MONO   = 0, FLV_STEREO = 1 };

typedef struct {
    uint8_t id;
    uint8_t profile_idc;
    uint8_t level_idc;
    uint8_t constraint_set_flags;
    uint8_t chroma_format_idc;
    uint8_t bit_depth_luma;
    uint8_t bit_depth_chroma;
    uint8_t frame_mbs_only_flag;
    AVRational sar;
} H264SPS;

/* duplicated from avpriv_mpeg4audio_sample_rates to avoid shared build
 * failures */
static const int mpeg4audio_sample_rates[16] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
};

#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
uint32_t d = (val);                     \
((uint8_t*)(p))[3] = (d);               \
((uint8_t*)(p))[2] = (d)>>8;            \
((uint8_t*)(p))[1] = (d)>>16;           \
((uint8_t*)(p))[0] = (d)>>24;           \
} while(0)
#endif

#if ARCH_X86_64
// TODO: Benchmark and optionally enable on other 64-bit architectures.
typedef uint64_t BitBuf;
#define AV_WBBUF AV_WB64
#define AV_WLBUF AV_WL64
#else
typedef uint32_t BitBuf;
#define AV_WBBUF AV_WB32
#define AV_WLBUF AV_WL32
#endif

static const AVRational avc_sample_aspect_ratio[17] = {
        {   0,  1 },
        {   1,  1 },
        {  12, 11 },
        {  10, 11 },
        {  16, 11 },
        {  40, 33 },
        {  24, 11 },
        {  20, 11 },
        {  32, 11 },
        {  80, 33 },
        {  18, 11 },
        {  15, 11 },
        {  64, 33 },
        { 160, 99 },
        {   4,  3 },
        {   3,  2 },
        {   2,  1 },
};

typedef struct PutBitContext {
    BitBuf bit_buf;
    int bit_left;
    uint8_t *buf, *buf_ptr, *buf_end;
} PutBitContext;

static const int BUF_BITS = 8 * sizeof(BitBuf);

/**
 * Initialize the PutBitContext s.
 *
 * @param buffer the buffer where to put bits
 * @param buffer_size the size in bytes of buffer
 */
static inline void init_put_bits(PutBitContext *s, uint8_t *buffer, int buffer_size)
{
    if (buffer_size < 0) {
        buffer_size = 0;
        buffer      = NULL;
    }

    s->buf          = buffer;
    s->buf_end      = s->buf + buffer_size;
    s->buf_ptr      = s->buf;
    s->bit_left     = BUF_BITS;
    s->bit_buf      = 0;
}

static inline void put_bits_no_assert(PutBitContext *s, int n, BitBuf value)
{
    BitBuf bit_buf;
    int bit_left;

    bit_buf  = s->bit_buf;
    bit_left = s->bit_left;

    /* XXX: optimize */
#ifdef BITSTREAM_WRITER_LE
    bit_buf |= value << (BUF_BITS - bit_left);
    if (n >= bit_left) {
        if (s->buf_end - s->buf_ptr >= sizeof(BitBuf)) {
            AV_WLBUF(s->buf_ptr, bit_buf);
            s->buf_ptr += sizeof(BitBuf);
        } else {
            av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
            av_assert2(0);
        }
        bit_buf     = value >> bit_left;
        bit_left   += BUF_BITS;
    }
    bit_left -= n;
#else
    if (n < bit_left) {
        bit_buf     = (bit_buf << n) | value;
        bit_left   -= n;
    } else {
        bit_buf   <<= bit_left;
        bit_buf    |= value >> (n - bit_left);
        if (s->buf_end - s->buf_ptr >= sizeof(BitBuf)) {
            AV_WBBUF(s->buf_ptr, bit_buf);
            s->buf_ptr += sizeof(BitBuf);
        } else {
            av_log(NULL, AV_LOG_ERROR, "Internal error, put_bits buffer too small\n");
            av_assert2(0);
        }
        bit_left   += BUF_BITS - n;
        bit_buf     = value;
    }
#endif

s->bit_buf  = bit_buf;
    s->bit_left = bit_left;
}

/**
 * Write up to 31 bits into a bitstream.
 * Use put_bits32 to write 32 bits.
 */
static inline void put_bits(PutBitContext *s, int n, BitBuf value)
{
    av_assert2(n <= 31 && value < (1UL << n));
    put_bits_no_assert(s, n, value);
}

/**
 * Pad the end of the output stream with zeros.
 */
static inline void flush_put_bits(PutBitContext *s)
{
#ifndef BITSTREAM_WRITER_LE
    if (s->bit_left < BUF_BITS)
        s->bit_buf <<= s->bit_left;
#endif
    while (s->bit_left < BUF_BITS) {
        av_assert0(s->buf_ptr < s->buf_end);
#ifdef BITSTREAM_WRITER_LE
        *s->buf_ptr++ = s->bit_buf;
        s->bit_buf  >>= 8;
#else
        *s->buf_ptr++ = s->bit_buf >> (BUF_BITS - 8);
        s->bit_buf  <<= 8;
#endif
        s->bit_left  += 8;
    }
    s->bit_left = BUF_BITS;
    s->bit_buf  = 0;
}

typedef struct FLVFileposition {
    int64_t keyframe_position;
    double keyframe_timestamp;
    struct FLVFileposition *next;
} FLVFileposition;

typedef struct FLVContext {
    AVClass *av_class;
    int     reserved;
    int64_t duration_offset;
    int64_t filesize_offset;
    int64_t duration;
    int64_t delay;      ///< first dts delay (needed for AVC & Speex)

    int64_t datastart_offset;
    int64_t datasize_offset;
    int64_t datasize;
    int64_t videosize_offset;
    int64_t videosize;
    int64_t audiosize_offset;
    int64_t audiosize;

    int64_t metadata_size_pos;
    int64_t metadata_totalsize_pos;
    int64_t metadata_totalsize;
    int64_t keyframe_index_size;

    int64_t lasttimestamp_offset;
    double lasttimestamp;
    int64_t lastkeyframetimestamp_offset;
    double lastkeyframetimestamp;
    int64_t lastkeyframelocation_offset;
    int64_t lastkeyframelocation;

    int acurframeindex;
    int64_t keyframes_info_offset;

    int64_t filepositions_count;
    FLVFileposition *filepositions;
    FLVFileposition *head_filepositions;

    AVCodecParameters *audio_par;
    AVCodecParameters *video_par;
    double framerate;
    AVCodecParameters *data_par;

    int flags;
} FLVContext;

static inline void push_wb16(std::vector<uint8_t>& v, unsigned int val)
{
    v.emplace_back( (int)val >> 8);
    v.emplace_back( (uint8_t)val);
}

static inline void replace_wb16(std::vector<uint8_t>& v, size_t pos, unsigned int val)
{
    v[pos] =    (int)val >> 8;
    v[pos +1] = (uint8_t)val;
}

static inline void push_wb24(std::vector<uint8_t>& v, unsigned int val)
{
    push_wb16(v, (int)val >> 8);
    v.emplace_back((uint8_t)val);
}

static inline void replace_wb24(std::vector<uint8_t>& v,  size_t pos, unsigned int val)
{
    replace_wb16(v, pos, (int)val >> 8);
    v[pos +2] = (uint8_t)val;
}

static inline void push_wb32(std::vector<uint8_t>& v, unsigned int val)
{
    v.emplace_back(         val >> 24);
    v.emplace_back((uint8_t)(val >> 16));
    v.emplace_back((uint8_t)(val >> 8 ));
    v.emplace_back((uint8_t) val       );
}

static inline void replace_wb32(std::vector<uint8_t>& v, size_t pos, unsigned int val)
{
    v[pos] =             val >> 24;
    v[pos+1] = (uint8_t)(val >> 16);
    v[pos+2] = (uint8_t)(val >> 8 );
    v[pos+3] = (uint8_t) val       ;
}

static inline void push_wb64(std::vector<uint8_t>& v, uint64_t val)
{
    push_wb32(v, (uint32_t)(val >> 32));
    push_wb32(v, (uint32_t)(val & 0xffffffff));
}

static inline void push_str(std::vector<uint8_t>& v, std::string str)
{
    v.insert(v.end(), str.begin(), str.end());
}

static void push_double(std::vector<uint8_t>& v, double d)
{
    v.emplace_back(AMF_DATA_TYPE_NUMBER);
    push_wb64(v, av_double2int(d));
}

static void push_bool(std::vector<uint8_t>& v, int b)
{
    v.emplace_back(AMF_DATA_TYPE_BOOL);
    v.emplace_back(!!b);
}

#if !HAVE_GMTIME_R && !defined(gmtime_r)
static inline struct tm *ff_gmtime_r(const time_t* clock, struct tm *result)
{
    struct tm *ptr = gmtime(clock);
    if (!ptr)
        return NULL;
    *result = *ptr;
    return result;
}
#define gmtime_r ff_gmtime_r
#endif

static int ff_parse_creation_time_metadata(AVFormatContext *s, int64_t *timestamp, int return_seconds)
{
    AVDictionaryEntry *entry;
    int64_t parsed_timestamp;
    int ret;
    if ((entry = av_dict_get(s->metadata, "creation_time", NULL, 0))) {
        if ((ret = av_parse_time(&parsed_timestamp, entry->value, 0)) >= 0) {
            *timestamp = return_seconds ? parsed_timestamp / 1000000 : parsed_timestamp;
            return 1;
        } else {
            av_log(s, AV_LOG_WARNING, "Failed to parse creation_time %s\n", entry->value);
            return ret;
        }
    }
    return 0;
}

static int avpriv_dict_set_timestamp(AVDictionary **dict, const char *key, int64_t timestamp)
{
    time_t seconds = timestamp / 1000000;
    struct tm *ptm, tmbuf;
    ptm = gmtime_r(&seconds, &tmbuf);
    if (ptm) {
        char buf[32];
        if (!strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", ptm))
            return AVERROR_EXTERNAL;
        av_strlcatf(buf, sizeof(buf), ".%06dZ", (int)(timestamp % 1000000));
        return av_dict_set(dict, key, buf, 0);
    } else {
        return AVERROR_EXTERNAL;
    }
}

static int ff_standardize_creation_time(AVFormatContext *s)
{
    int64_t timestamp;
    int ret = ff_parse_creation_time_metadata(s, &timestamp, 0);
    if (ret == 1)
        return avpriv_dict_set_timestamp(&s->metadata, "creation_time", timestamp);
    return ret;
}

static void put_timestamp(std::vector<uint8_t>& packet, int64_t ts)
{
    push_wb24(packet, ts & 0xFFFFFF);
    packet.emplace_back((ts >> 24) & 0x7F);
}

static int get_audio_flags(AVFormatContext *s, AVCodecParameters *par)
{
    int flags = (par->bits_per_coded_sample == 16) ? FLV_SAMPLESSIZE_16BIT : FLV_SAMPLESSIZE_8BIT;

    if (par->codec_id == AV_CODEC_ID_AAC) // specs force these parameters
        return FLV_CODECID_AAC | FLV_SAMPLERATE_44100HZ | FLV_SAMPLESSIZE_16BIT | FLV_STEREO;
    else if (par->codec_id == AV_CODEC_ID_SPEEX) {
        if (par->sample_rate != 16000) {
            av_log(s, AV_LOG_ERROR,
                   "FLV only supports wideband (16kHz) Speex audio\n");
            return AVERROR(EINVAL);
        }
        if (par->channels != 1) {
            av_log(s, AV_LOG_ERROR, "FLV only supports mono Speex audio\n");
            return AVERROR(EINVAL);
        }
        return FLV_CODECID_SPEEX | FLV_SAMPLERATE_11025HZ | FLV_SAMPLESSIZE_16BIT;
    } else {
        switch (par->sample_rate) {
            case 48000:
                // 48khz mp3 is stored with 44k1 samplerate identifer
                if (par->codec_id == AV_CODEC_ID_MP3) {
                    flags |= FLV_SAMPLERATE_44100HZ;
                    break;
                } else {
                    goto error;
                }
            case 44100:
                flags |= FLV_SAMPLERATE_44100HZ;
                break;
            case 22050:
                flags |= FLV_SAMPLERATE_22050HZ;
                break;
            case 11025:
                flags |= FLV_SAMPLERATE_11025HZ;
                break;
            case 16000: // nellymoser only
            case  8000: // nellymoser only
            case  5512: // not MP3
                if (par->codec_id != AV_CODEC_ID_MP3) {
                    flags |= FLV_SAMPLERATE_SPECIAL;
                    break;
                }
            default:
                error:
            av_log(s, AV_LOG_ERROR,
                   "FLV does not support sample rate %d, "
                   "choose from (44100, 22050, 11025)\n", par->sample_rate);
                                return AVERROR(EINVAL);
        }
    }

    if (par->channels > 1)
        flags |= FLV_STEREO;

    switch (par->codec_id) {
        case AV_CODEC_ID_MP3:
            flags |= FLV_CODECID_MP3    | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_PCM_U8:
            flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_8BIT;
            break;
        case AV_CODEC_ID_PCM_S16BE:
            flags |= FLV_CODECID_PCM    | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_PCM_S16LE:
            flags |= FLV_CODECID_PCM_LE | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_ADPCM_SWF:
            flags |= FLV_CODECID_ADPCM  | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_NELLYMOSER:
            if (par->sample_rate == 8000)
                flags |= FLV_CODECID_NELLYMOSER_8KHZ_MONO  | FLV_SAMPLESSIZE_16BIT;
            else if (par->sample_rate == 16000)
                flags |= FLV_CODECID_NELLYMOSER_16KHZ_MONO | FLV_SAMPLESSIZE_16BIT;
            else
                flags |= FLV_CODECID_NELLYMOSER            | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_PCM_MULAW:
            flags = FLV_CODECID_PCM_MULAW | FLV_SAMPLERATE_SPECIAL | FLV_SAMPLESSIZE_16BIT;
            break;
        case AV_CODEC_ID_PCM_ALAW:
            flags = FLV_CODECID_PCM_ALAW  | FLV_SAMPLERATE_SPECIAL | FLV_SAMPLESSIZE_16BIT;
            break;
        case 0:
            flags |= par->codec_tag << 4;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Audio codec '%s' not compatible with FLV\n",
                   avcodec_get_name(par->codec_id));
            return AVERROR(EINVAL);
    }

    return flags;
}

static uint8_t *ff_nal_unit_extract_rbsp(const uint8_t *src, uint32_t src_len, uint32_t *dst_len, uint32_t header_len)
{
    uint8_t *dst;
    uint32_t i, len;

    dst = (uint8_t*) av_malloc(src_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!dst)
        return NULL;

    /* NAL unit header */
    i = len = 0;
    while (i < header_len && i < src_len)
        dst[len++] = src[i++];

    while (i + 2 < src_len)
        if (!src[i] && !src[i + 1] && src[i + 2] == 3) {
            dst[len++] = src[i++];
            dst[len++] = src[i++];
            i++; // remove emulation_prevention_three_byte
        } else
            dst[len++] = src[i++];

    while (i < src_len)
        dst[len++] = src[i++];

    memset(dst + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *dst_len = len;
    return dst;
}

const uint8_t ff_golomb_vlc_len[512]={
        19,17,15,15,13,13,13,13,11,11,11,11,11,11,11,11,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

const uint8_t ff_ue_golomb_vlc_code[512]={
        32,32,32,32,32,32,32,32,31,32,32,32,32,32,32,32,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,
        7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9,10,10,10,10,11,11,11,11,12,12,12,12,13,13,13,13,14,14,14,14,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const int8_t ff_se_golomb_vlc_code[512]={
        17, 17, 17, 17, 17, 17, 17, 17, 16, 17, 17, 17, 17, 17, 17, 17,  8, -8,  9, -9, 10,-10, 11,-11, 12,-12, 13,-13, 14,-14, 15,-15,
        4,  4,  4,  4, -4, -4, -4, -4,  5,  5,  5,  5, -5, -5, -5, -5,  6,  6,  6,  6, -6, -6, -6, -6,  7,  7,  7,  7, -7, -7, -7, -7,
        2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};



    /**
 * Read an unsigned Exp-Golomb code in the range 0 to 8190.
 *
 * @returns the read value or a negative error code.
 */
static inline int get_ue_golomb(GetBitContext *gb)
{
    unsigned int buf;

#if CACHED_BITSTREAM_READER
    buf = show_bits_long(gb, 32);

if (buf >= (1 << 27)) {
    buf >>= 32 - 9;
    skip_bits_long(gb, ff_golomb_vlc_len[buf]);

    return ff_ue_golomb_vlc_code[buf];
} else {
    int log = 2 * av_log2(buf) - 31;

    skip_bits_long(gb, 32 - log);
    if (log < 7)
        return AVERROR_INVALIDDATA;
    buf >>= log;
    buf--;

    return buf;
}
#else
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf = GET_CACHE(re, gb);

    if (buf >= (1 << 27)) {
        buf >>= 32 - 9;
        LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_ue_golomb_vlc_code[buf];
    } else {
        int log = 2 * av_log2(buf) - 31;
        LAST_SKIP_BITS(re, gb, 32 - log);
        CLOSE_READER(re, gb);
        if (log < 7)
            return AVERROR_INVALIDDATA;
        buf >>= log;
        buf--;

        return buf;
    }
#endif
}


/**
* read signed exp golomb code.
*/
static inline int get_se_golomb(GetBitContext *gb)
{
    unsigned int buf;

#if CACHED_BITSTREAM_READER
    buf = show_bits_long(gb, 32);

if (buf >= (1 << 27)) {
    buf >>= 32 - 9;
    skip_bits_long(gb, ff_golomb_vlc_len[buf]);

    return ff_se_golomb_vlc_code[buf];
} else {
    int log = 2 * av_log2(buf) - 31;
    buf >>= log;

    skip_bits_long(gb, 32 - log);

    if (buf & 1)
        buf = -(buf >> 1);
    else
        buf = (buf >> 1);

    return buf;
}
#else
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf = GET_CACHE(re, gb);

    if (buf >= (1 << 27)) {
        buf >>= 32 - 9;
        LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_se_golomb_vlc_code[buf];
    } else {
        int log = av_log2(buf), sign;
        LAST_SKIP_BITS(re, gb, 31 - log);
        UPDATE_CACHE(re, gb);
        buf = GET_CACHE(re, gb);

        buf >>= log;

        LAST_SKIP_BITS(re, gb, 32 - log);
        CLOSE_READER(re, gb);

        sign = -(buf & 1);
        buf  = ((buf >> 1) ^ sign) - sign;

        return buf;
    }
#endif
}


static int ff_avc_decode_sps(H264SPS *sps, const uint8_t *buf, int buf_size)
{
    int i, j, ret, rbsp_size, aspect_ratio_idc, pic_order_cnt_type;
    int num_ref_frames_in_pic_order_cnt_cycle;
    int delta_scale, lastScale = 8, nextScale = 8;
    int sizeOfScalingList;
    GetBitContext gb;
    uint8_t *rbsp_buf;

    rbsp_buf = ff_nal_unit_extract_rbsp(buf, buf_size, reinterpret_cast<uint32_t *>(&rbsp_size), 0);
    if (!rbsp_buf)
        return AVERROR(ENOMEM);

    ret = init_get_bits8(&gb, rbsp_buf, rbsp_size);
    if (ret < 0)
        goto end;

    memset(sps, 0, sizeof(*sps));

    sps->profile_idc = get_bits(&gb, 8);
    sps->constraint_set_flags |= get_bits1(&gb) << 0; // constraint_set0_flag
    sps->constraint_set_flags |= get_bits1(&gb) << 1; // constraint_set1_flag
    sps->constraint_set_flags |= get_bits1(&gb) << 2; // constraint_set2_flag
    sps->constraint_set_flags |= get_bits1(&gb) << 3; // constraint_set3_flag
    sps->constraint_set_flags |= get_bits1(&gb) << 4; // constraint_set4_flag
    sps->constraint_set_flags |= get_bits1(&gb) << 5; // constraint_set5_flag
    skip_bits(&gb, 2); // reserved_zero_2bits
    sps->level_idc = get_bits(&gb, 8);
    sps->id = get_ue_golomb(&gb);

    if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
        sps->profile_idc == 122 || sps->profile_idc == 244 || sps->profile_idc ==  44 ||
        sps->profile_idc ==  83 || sps->profile_idc ==  86 || sps->profile_idc == 118 ||
        sps->profile_idc == 128 || sps->profile_idc == 138 || sps->profile_idc == 139 ||
        sps->profile_idc == 134) {
        sps->chroma_format_idc = get_ue_golomb(&gb); // chroma_format_idc
        if (sps->chroma_format_idc == 3) {
            skip_bits1(&gb); // separate_colour_plane_flag
        }
        sps->bit_depth_luma = get_ue_golomb(&gb) + 8;
        sps->bit_depth_chroma = get_ue_golomb(&gb) + 8;
        skip_bits1(&gb); // qpprime_y_zero_transform_bypass_flag
        if (get_bits1(&gb)) { // seq_scaling_matrix_present_flag
            for (i = 0; i < ((sps->chroma_format_idc != 3) ? 8 : 12); i++) {
                if (!get_bits1(&gb)) // seq_scaling_list_present_flag
                    continue;
                lastScale = 8;
                nextScale = 8;
                sizeOfScalingList = i < 6 ? 16 : 64;
                for (j = 0; j < sizeOfScalingList; j++) {
                    if (nextScale != 0) {
                        delta_scale = get_se_golomb(&gb);
                        nextScale = (lastScale + delta_scale) & 0xff;
                    }
                    lastScale = nextScale == 0 ? lastScale : nextScale;
                }
            }
        }
    } else {
        sps->chroma_format_idc = 1;
        sps->bit_depth_luma = 8;
        sps->bit_depth_chroma = 8;
    }

    get_ue_golomb(&gb); // log2_max_frame_num_minus4
    pic_order_cnt_type = get_ue_golomb(&gb);

    if (pic_order_cnt_type == 0) {
        get_ue_golomb(&gb); // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
        skip_bits1(&gb);    // delta_pic_order_always_zero
        get_se_golomb(&gb); // offset_for_non_ref_pic
        get_se_golomb(&gb); // offset_for_top_to_bottom_field
        num_ref_frames_in_pic_order_cnt_cycle = get_ue_golomb(&gb);
        for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
            get_se_golomb(&gb); // offset_for_ref_frame
    }

    get_ue_golomb(&gb); // max_num_ref_frames
    skip_bits1(&gb); // gaps_in_frame_num_value_allowed_flag
    get_ue_golomb(&gb); // pic_width_in_mbs_minus1
    get_ue_golomb(&gb); // pic_height_in_map_units_minus1

    sps->frame_mbs_only_flag = get_bits1(&gb);
    if (!sps->frame_mbs_only_flag)
        skip_bits1(&gb); // mb_adaptive_frame_field_flag

    skip_bits1(&gb); // direct_8x8_inference_flag

    if (get_bits1(&gb)) { // frame_cropping_flag
        get_ue_golomb(&gb); // frame_crop_left_offset
        get_ue_golomb(&gb); // frame_crop_right_offset
        get_ue_golomb(&gb); // frame_crop_top_offset
        get_ue_golomb(&gb); // frame_crop_bottom_offset
    }

    if (get_bits1(&gb)) { // vui_parameters_present_flag
        if (get_bits1(&gb)) { // aspect_ratio_info_present_flag
            aspect_ratio_idc = get_bits(&gb, 8);
            if (aspect_ratio_idc == 0xff) {
                sps->sar.num = get_bits(&gb, 16);
                sps->sar.den = get_bits(&gb, 16);
            } else if (aspect_ratio_idc < FF_ARRAY_ELEMS(avc_sample_aspect_ratio)) {
                sps->sar = avc_sample_aspect_ratio[aspect_ratio_idc];
            }
        }
    }

    if (!sps->sar.den) {
        sps->sar.num = 1;
        sps->sar.den = 1;
    }

    ret = 0;
    end:
    av_free(rbsp_buf);
    return ret;
}

typedef struct DynBuffer {
    int pos, size, allocated_size;
    uint8_t *buffer;
    int io_buffer_size;
    uint8_t io_buffer[1];
} DynBuffer;

static void ffio_free_dyn_buf(AVIOContext **s)
{
    DynBuffer *d;

    if (!*s)
        return;

    d = static_cast<DynBuffer *>((*s)->opaque);
    av_free(d->buffer);
    avio_context_free(s);
}

static const uint8_t *avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

static const uint8_t *ff_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *out = avc_find_startcode_internal(p, end);
    if(p<out && out<end && !out[-1]) out--;
    return out;
}

static int ff_avc_parse_nal_units(AVIOContext *pb, const uint8_t *buf_in, int size)
{
    const uint8_t *p = buf_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start, *nal_end;

    size = 0;
    nal_start = ff_avc_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = ff_avc_find_startcode(nal_start, end);
        avio_wb32(pb, nal_end - nal_start);
        avio_write(pb, nal_start, nal_end - nal_start);
        size += 4 + nal_end - nal_start;
        nal_start = nal_end;
    }
    return size;
}

static int ff_avc_parse_nal_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size)
{
    AVIOContext *pb;
    int ret = avio_open_dyn_buf(&pb);
    if(ret < 0)
        return ret;

    ff_avc_parse_nal_units(pb, buf_in, *size);

    *size = avio_close_dyn_buf(pb, buf);
    return 0;
}

static int ff_isom_write_avcc(AVIOContext *pb, std::vector<uint8_t>& packet, const uint8_t *data, int len)
{
    uint8_t *buf, *end, *start;
    int ret, nb_sps = 0, nb_pps = 0, nb_sps_ext = 0;

    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    std::vector<uint8_t> sps_ext;

    if (len <= 6)
        return AVERROR_INVALIDDATA;

    /* check for H.264 start code */
    if (AV_RB32(data) != 0x00000001 && AV_RB24(data) != 0x000001) {
        packet.resize(packet.size() + len);
        memcpy(&packet[packet.size() -len -1], data, len);
        return 0;
    }

    ret = ff_avc_parse_nal_units_buf(data, &buf, &len);
    if (ret < 0)
        return ret;
    start = buf;
    end = buf + len;

    /* look for sps and pps */
    while (end - buf > 4)
    {
        uint32_t size;
        uint8_t nal_type;
        size = FFMIN(AV_RB32(buf), end - buf - 4);
        buf += 4;
        nal_type = buf[0] & 0x1f;

        if (nal_type == 7) /* SPS */
        {
            nb_sps++;
            if (size > UINT16_MAX || nb_sps >= H264_MAX_SPS_COUNT) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            push_wb16(sps, size);
            sps.resize(sps.size() + size);
            memcpy(&sps[sps.size() -size -1], buf, size);

        } else if (nal_type == 8) { /* PPS */
            nb_pps++;
            if (size > UINT16_MAX || nb_pps >= H264_MAX_PPS_COUNT) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            push_wb16(pps, size);
            pps.resize(pps.size() + size);
            memcpy(&pps[pps.size() -size -1], buf, size);

        } else if (nal_type == 13) { /* SPS_EXT */
            nb_sps_ext++;
            if (size > UINT16_MAX || nb_sps_ext >= 256) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            push_wb16(sps_ext, size);
            sps_ext.resize(sps_ext.size() + size);
            memcpy(&sps_ext[sps_ext.size() -size -1], buf, size);
        }

        buf += size;
    }

    if (sps.size() < 6 || pps.empty()) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    packet.emplace_back( 1); /* version */
    packet.emplace_back(sps[3]); /* profile */
    packet.emplace_back(sps[4]); /* profile compat */
    packet.emplace_back(sps[5]); /* level */
    packet.emplace_back(0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
    packet.emplace_back(0xe0 | nb_sps); /* 3 bits reserved (111) + 5 bits number of sps */

    packet.insert(packet.end(), sps.begin(), sps.end());
    packet.emplace_back(nb_pps); /* number of pps */
    packet.insert(packet.end(), pps.begin(), pps.end());

    if (sps[3] != 66 && sps[3] != 77 && sps[3] != 88) {
        H264SPS seq;
        ret = ff_avc_decode_sps(&seq, &sps[0] + 3, (int)sps.size() - 3);
        if (ret < 0)
            goto fail;

        packet.emplace_back(0xfc |  seq.chroma_format_idc); /* 6 bits reserved (111111) + chroma_format_idc */
        packet.emplace_back(0xf8 | (seq.bit_depth_luma - 8)); /* 5 bits reserved (11111) + bit_depth_luma_minus8 */
        packet.emplace_back(0xf8 | (seq.bit_depth_chroma - 8)); /* 5 bits reserved (11111) + bit_depth_chroma_minus8 */
        packet.emplace_back(nb_sps_ext); /* number of sps ext */
        if (nb_sps_ext)
            packet.insert(packet.end(), sps_ext.begin(), sps_ext.end());
    }

fail:
    av_free(start);
    return ret;
}

static void flv_write_codec_header(AVFormatContext* s, AVCodecParameters* par, int64_t ts, std::vector<uint8_t>& packet, RTMP* rtmpHnd)
{
    int64_t data_size;
    FLVContext *flv = (FLVContext*) s->priv_data;
    AVIOContext *pb = s->pb;

    if (par->codec_id == AV_CODEC_ID_AAC || par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_MPEG4)
    {
        int64_t pos;
        packet.emplace_back(par->codec_type == AVMEDIA_TYPE_VIDEO ? FLV_TAG_TYPE_VIDEO : FLV_TAG_TYPE_AUDIO);
        push_wb24(packet, 0); // size patched later
        put_timestamp(packet, ts);
        push_wb24(packet, 0); // streamid
        pos = packet.size() -1;

        if (par->codec_id == AV_CODEC_ID_AAC)
        {
            packet.emplace_back(get_audio_flags(s, par));
            packet.emplace_back(0); // AAC sequence header

            if (!par->extradata_size && (flv->flags & FLV_AAC_SEQ_HEADER_DETECT))
            {
                PutBitContext pbc;
                int samplerate_index;
                int channels = flv->audio_par->channels - (flv->audio_par->channels == 8 ? 1 : 0);
                uint8_t data[2];

                for (samplerate_index = 0; samplerate_index < 16; samplerate_index++)
                    if (flv->audio_par->sample_rate == mpeg4audio_sample_rates[samplerate_index])
                        break;

                init_put_bits(&pbc, data, sizeof(data));
                put_bits(&pbc, 5, flv->audio_par->profile + 1); //profile
                put_bits(&pbc, 4, samplerate_index); //sample rate index
                put_bits(&pbc, 4, channels);
                put_bits(&pbc, 1, 0); //frame length - 1024 samples
                put_bits(&pbc, 1, 0); //does not depend on core coder
                put_bits(&pbc, 1, 0); //is not extension
                flush_put_bits(&pbc);

                packet.emplace_back(data[0]);
                packet.emplace_back(data[1]);

                av_log(s, AV_LOG_WARNING, "AAC sequence header: %02x %02x.\n", data[0], data[1]);
            }

            if (RTMP_Write(rtmpHnd, (char*) &packet[0], (int)packet.size(), 0) < 0)
                LOGE << "flv_write_codec_header error: failed to write codec header ";

            if (RTMP_Write(rtmpHnd, (char*) par->extradata, par->extradata_size, 0) < 0)
                LOGE << "flv_write_codec_header error: failed to write extra data ";

        } else {
            packet.push_back(par->codec_tag | FLV_FRAME_KEY); // flags
            packet.push_back(0); // AVC sequence header
            push_wb24(packet, 0); // composition time
            ff_isom_write_avcc(pb, packet, par->extradata, par->extradata_size);
        }

        data_size = packet.size() - pos;

        replace_wb24(packet, 0, data_size);
        push_wb32(packet, data_size + 11); // previous tag size
    }
}

static void write_metadata(AVFormatContext *s, unsigned int ts, std::vector<uint8_t>& packet)
{
    AVIOContext *pb = s->pb;
    auto flv = (ffmpeg::FLVContext*)s->priv_data;
    int write_duration_filesize = !(flv->flags & FLV_NO_DURATION_FILESIZE);
    int metadata_count = 0;
    int64_t metadata_count_pos;
    AVDictionaryEntry *tag = nullptr;

    /* write meta_tag */
    packet.emplace_back(FLV_TAG_TYPE_META);            // tag type META
    flv->metadata_size_pos = packet.size();
    push_wb24(packet,  0);           // size of data part (sum of all parts below)
    push_wb24(packet,  ts);          // timestamp
    push_wb32(packet,  0);           // reserved

    /* now data of data_size size */

    /* first event name as a string */
    packet.emplace_back( AMF_DATA_TYPE_STRING);
    push_str(packet, "onMetaData"); // 12 bytes

    /* mixed array (hash) with size and string/type/data tuples */
    packet.emplace_back( AMF_DATA_TYPE_MIXEDARRAY);
    metadata_count_pos = packet.size();
    metadata_count = 4 * !!flv->video_par +
                     5 * !!flv->audio_par +
                     1 * !!flv->data_par;
    if (write_duration_filesize) {
        metadata_count += 2; // +2 for duration and file size
    }
    push_wb32(packet,  metadata_count);

    if (write_duration_filesize)
    {
        push_str(packet, "duration");
        flv->duration_offset = packet.size();
        // fill in the guessed duration, it'll be corrected later if incorrect
        push_double(packet, s->duration / AV_TIME_BASE);
    }

    if (flv->video_par)
    {
        push_str(packet, "width");
        push_double(packet, flv->video_par->width);

        push_str(packet, "height");
        push_double(packet, flv->video_par->height);

        push_str(packet, "videodatarate");
        push_double(packet, flv->video_par->bit_rate / 1024.0);

        if (flv->framerate != 0.0) {
            push_str(packet, "framerate");
            push_double(packet, flv->framerate);
            metadata_count++;
        }

        push_str(packet, "videocodecid");
        push_double(packet, flv->video_par->codec_tag);
    }

    if (flv->audio_par) {
        push_str(packet, "audiodatarate");
        push_double(packet, flv->audio_par->bit_rate / 1024.0);

        push_str(packet, "audiosamplerate");
        push_double(packet, flv->audio_par->sample_rate);

        push_str(packet, "audiosamplesize");
        push_double(packet, flv->audio_par->codec_id == AV_CODEC_ID_PCM_U8 ? 8 : 16);

        push_str(packet, "stereo");
        push_bool(packet, flv->audio_par->channels == 2);

        push_str(packet, "audiocodecid");
        push_double(packet, flv->audio_par->codec_tag);
    }

    if (flv->data_par) {
        push_str(packet, "datastream");
        push_double(packet, 0.0);
    }

    ff_standardize_creation_time(s);
    while ((tag = av_dict_get(s->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        if(   !strcmp(tag->key, "width")
              ||!strcmp(tag->key, "height")
              ||!strcmp(tag->key, "videodatarate")
              ||!strcmp(tag->key, "framerate")
              ||!strcmp(tag->key, "videocodecid")
              ||!strcmp(tag->key, "audiodatarate")
              ||!strcmp(tag->key, "audiosamplerate")
              ||!strcmp(tag->key, "audiosamplesize")
              ||!strcmp(tag->key, "stereo")
              ||!strcmp(tag->key, "audiocodecid")
              ||!strcmp(tag->key, "duration")
              ||!strcmp(tag->key, "onMetaData")
              ||!strcmp(tag->key, "datasize")
              ||!strcmp(tag->key, "lasttimestamp")
              ||!strcmp(tag->key, "totalframes")
              ||!strcmp(tag->key, "hasAudio")
              ||!strcmp(tag->key, "hasVideo")
              ||!strcmp(tag->key, "hasCuePoints")
              ||!strcmp(tag->key, "hasMetadata")
              ||!strcmp(tag->key, "hasKeyframes")
                ){
            av_log(s, AV_LOG_DEBUG, "Ignoring metadata for %s\n", tag->key);
            continue;
        }
        push_str(packet, tag->key);
        packet.emplace_back( AMF_DATA_TYPE_STRING);
        push_str(packet, tag->value);
        metadata_count++;
    }

    if (write_duration_filesize) {
        push_str(packet, "filesize");
        flv->filesize_offset = avio_tell(pb);
        push_double(packet, 0); // delayed write
    }

    if (flv->flags & FLV_ADD_KEYFRAME_INDEX) {
        flv->acurframeindex = 0;
        flv->keyframe_index_size = 0;

        push_str(packet, "hasVideo");
        push_bool(packet, !!flv->video_par);
        metadata_count++;

        push_str(packet, "hasKeyframes");
        push_bool(packet, 1);
        metadata_count++;

        push_str(packet, "hasAudio");
        push_bool(packet, !!flv->audio_par);
        metadata_count++;

        push_str(packet, "hasMetadata");
        push_bool(packet, 1);
        metadata_count++;

        push_str(packet, "canSeekToEnd");
        push_bool(packet, 1);
        metadata_count++;

        push_str(packet, "datasize");
        flv->datasize_offset = avio_tell(pb);
        flv->datasize = 0;
        push_double(packet, flv->datasize);
        metadata_count++;

        push_str(packet, "videosize");
        flv->videosize_offset = avio_tell(pb);
        flv->videosize = 0;
        push_double(packet, flv->videosize);
        metadata_count++;

        push_str(packet, "audiosize");
        flv->audiosize_offset = avio_tell(pb);
        flv->audiosize = 0;
        push_double(packet, flv->audiosize);
        metadata_count++;

        push_str(packet, "lasttimestamp");
        flv->lasttimestamp_offset = avio_tell(pb);
        flv->lasttimestamp = 0;
        push_double(packet, 0);
        metadata_count++;

        push_str(packet, "lastkeyframetimestamp");
        flv->lastkeyframetimestamp_offset = avio_tell(pb);
        flv->lastkeyframetimestamp = 0;
        push_double(packet, 0);
        metadata_count++;

        push_str(packet, "lastkeyframelocation");
        flv->lastkeyframelocation_offset = avio_tell(pb);
        flv->lastkeyframelocation = 0;
        push_double(packet, 0);
        metadata_count++;

        push_str(packet, "keyframes");
        packet.emplace_back(AMF_DATA_TYPE_OBJECT);
        metadata_count++;

        flv->keyframes_info_offset = packet.size();
    }

    push_str(packet, "");
    packet.emplace_back( AMF_END_OF_OBJECT);

    /* write total size of tag */
    flv->metadata_totalsize = packet.size() -1 - flv->metadata_size_pos - 10;

    replace_wb32(packet, metadata_count_pos, metadata_count);
    replace_wb24(packet, flv->metadata_size_pos, flv->metadata_totalsize);

    flv->metadata_totalsize_pos = packet.size();
    push_wb32(packet, flv->metadata_totalsize + 11);
}

static void flv_write_header(AVFormatContext *s, std::vector<uint8_t>& packet, bool hasAudio, bool hasVideo, RTMP* rtmpHnd)
{
    if (!rtmpHnd) return;
    AVIOContext *pb = s->pb;

    auto flv = (ffmpeg::FLVContext*)s->priv_data;
    packet.emplace_back('F');  // signature byte
    packet.emplace_back('L');  // signature byte
    packet.emplace_back('V');  // signature byte
    packet.emplace_back(1);    // file version

    // UB[5] must be 0
    packet.emplace_back(FLV_HEADER_FLAG_HASAUDIO * hasAudio +
                        FLV_HEADER_FLAG_HASVIDEO * hasVideo);

    ffmpeg::push_wb32(packet, 9); // data offset from start of file to start of body (that is size of header), usually 9 for FLV versino 1
    ffmpeg::push_wb32(packet, 0);

    for (unsigned int i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->codecpar->codec_tag == 5)
        {
            packet.emplace_back(8);                // message type / RTMP_PACKET_TYPE_AUDIO
            ffmpeg::push_wb24(packet, 0);   // include flags
            ffmpeg::push_wb24(packet, 0);   // time stamp
            ffmpeg::push_wb32(packet, 0);   // reserved
            ffmpeg::push_wb32(packet, 11);  // size
            flv->reserved = 5;
        }

    if (flv->flags & FLV_NO_METADATA) {
        pb->seekable = 0;
    } else {
        write_metadata(s, 0, packet);
    }

//    std::ofstream out("packet_dump.txt", std::ios::out | std::ios::binary);
//    out.write(reinterpret_cast<const char*>(packet.data()), packet.size());
//    out.close();

    //if (RTMP_Write(rtmpHnd, (char*) &packet[0], (int)packet.size(), 0) < 0)
      //  LOGE << "flv_write_header error: failed to write meta_data ";

    packet.clear();

    for (unsigned int i = 0; i < s->nb_streams; i++)
    {
        flv_write_codec_header(s, s->streams[i]->codecpar, 0, packet, rtmpHnd);

        if (RTMP_Write(rtmpHnd, (char*) &packet[0], (int)packet.size(), 0) < 0)
            LOGE << "flv_write_header error: failed to write codec_header ";

        packet.clear();
    }
}

#endif

}
#endif