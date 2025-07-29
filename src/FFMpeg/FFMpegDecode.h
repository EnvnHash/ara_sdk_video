/*
 * FFMpegDecode.h
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#pragma once

#ifdef ARA_USE_FFMPEG

#include <FFMpeg/FFMpegCommon.h>
#include <StopWatch.h>
#include <Conditional.h>
#include <AVCommon.h>
#include <Log.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#endif


#ifndef ARA_USE_GLBASE
namespace ara::glb
{
    class GLBase;
}
#else
#include <GLBase.h>
#endif

namespace ara::av
{

class FFMpegDecode
{
public:

    //FFMpegDecode()=default;
    //virtual ~FFMpegDecode()=default;

    int							        hw_decoder_init(AVCodecContext* ctx, enum AVHWDeviceType type);
    int							        OpenFile(GLBase* glbase, const std::string& filePath, int useNrThreads, int destWidth, int destHeight, bool useHwAccel, bool decodeYuv420OnGpu, bool doStart=false, std::function<void()> cb=nullptr);
#ifdef __ANDROID__
    int							        OpenAndroidAsset(glb::GLBase* glbase, struct android_app* app, std::string& assetName, int useNrThreads, int destWidth, int destHeight, bool useHwAccel, bool decodeYuv420OnGpu, bool doStart=false, std::function<void()> cb=nullptr);
    int                                 initMediaCode(AAsset* assetDescriptor);
    int                                 openAsset(AAsset* assetDescriptor);
    int                                 mediaCodecGetInputBuffer(AVPacket* packet);
    int                                 mediaCodecDequeueOutputBuffer();
    uint8_t*                            mediaCodecGetOutputBuffer(int status, size_t& size);
    void                                mediaCodecReleaseOutputBuffer(int status);
#endif
    static int                          read_packet_from_inbuf(void *opaque, uint8_t *buf, int buf_size);

    int							        OpenCamera(GLBase* glbase, std::string camName, int destWidth, int destHeight, bool decodeYuv420OnGpu=true);
    int                                 setupStreams(const AVInputFormat* format, AVDictionary** options, std::function<void()> initCb);
    int                                 allocateResources();
    AVDictionary**                      setup_find_stream_info_opts(AVFormatContext *s, AVDictionary *codec_opts);
    AVDictionary*                       filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id, AVFormatContext *s, AVStream *st, AVCodec *codec);
    int                                 check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);

    void						        start(double time);
    void 						        stop();
    void 						        setPause(bool val) { m_pause = val; }

    void 						        alloc_gl_res(AVPixelFormat _srcPixFmt);
    AVFrame*					        alloc_picture(enum AVPixelFormat pix_fmt, int width, int height, std::vector< std::vector<uint8_t> >::iterator buf);

    void 						        singleThreadDecodeLoop();
    int 						        decode_video_packet(AVPacket* pPacket, AVCodecContext* pCodecContext);
    int 						        decode_audio_packet(AVPacket* pPacket, AVCodecContext* pCodecContext);

    // 2 thread implementation of sending/receiving decoded frames, .... not much faster than the "while" variant
    //void 						        sendFrameLoop();
    //int 						        receiveFrameLoop();

    void 						        logging(const char* fmt, ...);
    void 						        log_callback(void* ptr, int level, const char* fmt, va_list vargs);

    double  					        get_duration_sec();
    double 						        get_fps();
    int64_t 					        get_total_frames();
    void                                dumpEncoders();
    void                                dumpDecoders();
    [[nodiscard]] inline uint32_t		getBitCount() const { return m_bitCount;  }
    inline double 						r2d(AVRational r) { return r.num == 0 || r.den == 0 ? 0. : (double) r.num / (double) r.den; }

    void 						        seek_frame(int64_t _frame_number, double time);
    inline void 						seek(double sec, double time) { seek_frame((int64_t) (sec * get_fps() + 0.5), time); }
    inline void						    resetToStart(double time) { seek(0.0, time); }

    uint8_t*                            reqNextBuf();

#ifdef ARA_USE_GLBASE
    void 						        initShader(AVPixelFormat _srcPixFmt);
    Shaders*				            getShader() { return m_shader; }
    void 						        shaderBegin();
    void 						        shaderEnd();

    void 						        loadFrameToTexture(double time);
    GLenum						        texture_pixel_format(AVPixelFormat srcFmt);

    inline std::vector<std::unique_ptr<Texture>>& getTextures() { return m_textures; }
    inline GLuint				        getTex() {  if (!m_textures.empty() && m_textures[0]->isAllocated()) return m_textures[0]->getId(); else return 0; }
    inline GLuint				        getTexU() { if (m_textures.size() > 1 && m_textures[1]->isAllocated()) return m_textures[1]->getId(); else return 0; }
    inline GLuint				        getTexV() { if (m_textures.size() > 2 && m_textures[2]->isAllocated()) return m_textures[2]->getId(); else return 0; }
#endif
    [[nodiscard]] inline bool			isRunning() const { return m_run; }
    [[nodiscard]] inline bool			isReady() const { return m_resourcesAllocated; }
    [[nodiscard]] inline unsigned short getNrAudioChannels() const{ return m_audio_nr_channels; }
    [[nodiscard]] inline unsigned short getNrVideoTracks() const{ return m_video_nr_tracks; }

    bool                                setAudioConverter(int destSampleRate, AVSampleFormat format);
    inline void					        setFirstVideoFrameCb(std::function<void()> cbFunc) { m_firstVideoFrameCb = std::move(cbFunc); }
    inline void					        setFirstAudioFrameCb(std::function<void()> cbFunc) { m_firstAudioFrameCb = std::move(cbFunc); }
    inline void					        setEndCbFunc(std::function<void()> cbFunc) { m_endCb = std::move(cbFunc); }
    inline void					        setAudioUpdtCb(std::function<void(audioCbData&)> cbFunc) { m_audioCb = std::move(cbFunc); }
    inline void					        setVideoUpdtCb(std::function<void(AVFrame*)> cbFunc) { m_videoCb = std::move(cbFunc); }
    inline void					        setVideoDecoderCb(std::function<void(uint8_t*)> cbFunc) { m_decodeCb = std::move(cbFunc); }
    inline void					        setVideoFrameBufferSize(int size) { m_videoFrameBufferSize = size; }

    inline void                         forceSampleRate(int sr) { m_forceSampleRate = sr; }
    inline void                         forceNrChannels(int nc) { m_forceNrChannels = nc; }
    void                                forceAudioCodec(const std::string& str);

    void                                clearResources();
    bool                                decodeYuv420OnGpu() { return m_decodeYuv420OnGpu; }

    [[nodiscard]] inline enum AVPixelFormat getSrcPixFmt() const { return m_srcPixFmt; }
    [[nodiscard]] inline int            getNrBufferedFrames() const { return (int)m_nrBufferedFrames; }
    int                                 getSampleRate() { return m_audio_codec_ctx ? (int)m_audio_codec_ctx->sample_rate : 0; }
    int                                 getVideoFrameBufferSize() { return m_videoFrameBufferSize; }
    int                                 getDecFramePtr() { return m_decFramePtr; }
    int                                 getUplFramePtr() { return m_frameToUpload; }
    std::atomic<bool>*                  getAudioQueueBlock() { return &m_audioQueueFull; }
    int                                 getFrameRateD() { return m_formatContext->streams[m_video_stream_index]->r_frame_rate.den; }
    int                                 getFrameRateN() { return m_formatContext->streams[m_video_stream_index]->r_frame_rate.num; }

    int 						        m_srcWidth=0;
    int 						        m_srcHeight=0;
    int 						        m_destWidth=0;
    int 						        m_destHeight=0;
    int 						        m_useNrThreads=4;

    enum AVPixelFormat			        m_srcPixFmt=(AVPixelFormat)0;
    enum AVPixelFormat			        m_destPixFmt=(AVPixelFormat)0;
    enum AVPixelFormat			        m_hwPixFmt=(AVPixelFormat)0;
    static inline enum AVPixelFormat    m_static_hwPixFmt=(AVPixelFormat)0;

    std::function<void(uint8_t*)>       m_downFrameCb;

protected:
    AVCodec*					        m_video_codec=nullptr;
    AVCodec*					        m_audio_codec=nullptr;
    AVCodecContext*				        m_video_codec_ctx=nullptr;
    AVCodecContext*                     m_audio_codec_ctx=nullptr;
    AVFormatContext*			        m_formatContext=nullptr;
    AVDictionary*                       m_format_opts=nullptr;
    AVDictionary*                       m_codec_opts=nullptr;

    AVFrame*					        m_frame = nullptr;
    AVFrame*					        m_audioFrame = nullptr;
    std::vector<AVFrame*>		        m_framePtr;
    std::vector<AVFrame*>		        m_bgraFrame;
    AVPacket*					        m_packet=nullptr;
    struct SwsContext*			        m_img_convert_ctx=nullptr;
    int                                 m_decFramePtr=0;

    enum AVHWDeviceType 		        m_hwDeviceType=(AVHWDeviceType)0;
    AVBufferRef*				        m_hw_device_ctx = nullptr;
    int64_t                             m_dstChannelLayout=0;
    enum AVSampleFormat                 m_dst_sample_fmt=(AVSampleFormat)0;
    struct SwrContext*                  m_audio_swr_ctx=nullptr;

#ifdef ARA_USE_GLBASE
    GLBase*		                        m_glbase=nullptr;
    ShaderCollector*		            m_shCol=nullptr;
    Shaders*				            m_shader=nullptr;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<GLuint>			        m_pbos;
#endif

    const char*                         m_audio_codec_name = nullptr;
    const char*                         m_video_codec_name = nullptr;
    const char*                         m_subtitle_codec_name = nullptr;
    const char*                         m_forced_audio_codec_name = nullptr;
    const char*                         m_forced_video_codec_name = nullptr;

    std::string					        m_filePath;
    AVCodecID					        m_forceVideoCodec=(AVCodecID)0;
    AVCodecID					        m_forceAudioCodec=(AVCodecID)0;
    std::thread					        m_decodeThread;
    std::mutex					        m_mutex;
    Conditional 	                    m_decodeCond;
    Conditional 	                    m_endThreadCond;

    std::function<void()>		        m_endCb;
    std::function<void()>		        m_firstVideoFrameCb;
    std::function<void()>		        m_firstAudioFrameCb;
    std::function<void(audioCbData&)>	m_audioCb;
    std::function<void(AVFrame*)>       m_videoCb;
    std::function<void(uint8_t*)>       m_decodeCb;

    bool						        m_gl_res_inited=false;
    bool						        m_useHwAccel=false;
    bool						        m_useAudioConversion=false;
    bool						        m_decodeYuv420OnGpu=false;
    bool						        m_resourcesAllocated=false;
    bool						        m_firstFramePresented=false;
    bool						        m_loop=true;
    bool   		                        m_run=false;
    bool   		                        m_pause=false;
    bool						        m_usePbos=false; // review memory leaks if using m_pbos....
    bool						        m_is_stream=false;
    bool						        m_hasNoTimeStamp=false;
    bool						        m_consumeFrames =false;
    bool						        m_gotFirstVideoFrame =false;
    bool						        m_gotFirstAudioFrame =false;

    std::atomic<bool>			        m_audioQueueFull =false;

    int							        m_logLevel=AV_LOG_INFO;
    int 						        m_video_stream_index=0;
    int                                 m_video_nr_tracks=0;
    int 						        m_audio_stream_index=0;
    int 						        m_audio_nr_channels=0;
    int 						        m_forceNrChannels=0;
    int 				                m_forceSampleRate=0;
    int 						        m_dstSampleRate=0;
    int 						        m_dst_nb_samples=0;
    int 						        m_max_dst_nb_samples=0;
    int 						        m_dst_audio_nb_channels=0;
    int 						        m_dst_audio_linesize=0;

    int                                 m_scan_all_pmts_set = 0;
    int                                 m_seek_by_bytes = -1;
    int                                 m_genpts=0;
    int                                 m_fps = 0;

    int64_t                             m_start_time = AV_NOPTS_VALUE;

    unsigned int				        m_nrTexBuffers=0;
    std::atomic<uint32_t>               m_nrBufferedFrames=0;
    uint32_t					        m_videoFrameBufferSize=32;
    uint32_t 				            m_nrFramesToStart=2;

    unsigned int				        m_nrPboBufs=3;
    unsigned int 				        m_pboIndex=0;
    unsigned int 				        m_actFrameNr=0;
    int 				                m_frameToUpload=-1;

    uint8_t**                           m_dst_sampleBuffer=nullptr;

    double 						        m_startTime=0.0;
    double 						        m_eps_zero=0.000025;
    double						        m_timeBaseDiv=0.0;
    double						        m_frameDur=0.0;
    double						        m_lastToGlTime=0.0;

    std::vector<double>				    m_ptss;
    double						        m_lastPtss=-1.0;

    unsigned int				        m_totNumFrames=0;
    std::vector<std::vector<uint8_t>>	m_buffer;
    std::vector<uint8_t>                m_memInputBuf;
    size_t                              m_avio_ctx_buffer_size=4096;
    AVIOContext*                        m_ioContext=nullptr;
    ffmpeg::memin_buffer_data           m_memin_buffer;

    uint32_t					        m_bitCount=8;
    audioCbData					        m_audioCbData;
    uint64_t                            m_videoStartPts=0;

    bool                                m_useMediaCodec = false;
#ifdef __ANDROID__
    std::vector<std::vector<uint8_t>>	m_rawBuffer;
    AMediaExtractor*                    m_mediaExtractor = nullptr;
    AMediaCodec*                        m_mediaCodec = nullptr;
    AVBitStreamFilter*                  m_bsf = nullptr;
    AVBSFContext*                       m_bsfCtx=nullptr;
    AMediaCodecBufferInfo               m_mediaCodecInfo;
#endif
};

}

#endif