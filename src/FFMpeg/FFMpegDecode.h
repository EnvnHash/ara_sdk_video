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
namespace ara::glb {
    class GLBase;
}
#else
#include <GLBase.h>
#endif

namespace ara::av {

class FFMpegDecode {
public:
    virtual void 				        openFile(const ffmpeg::DecodePar& p);
    virtual void   				        openCamera(const ffmpeg::DecodePar& p);
    virtual void    			        start(double time);
    virtual void 						stop();
    void 						        setPause(bool val) { m_pause = val; }

    // 2 thread implementation of sending/receiving decoded frames, .... not much faster than the "while" variant
    //void 						        sendFrameLoop();
    //int 						        receiveFrameLoop();

    void 						        seekFrame(int64_t frame_number, double time);
    inline void 						seek(double sec, double time) { seekFrame((int64_t) (sec * getFps() + 0.5), time); }
    inline void						    resetToStart(double time) { seek(0.0, time); }

    [[nodiscard]] inline bool			isRunning() const { return m_run; }
    [[nodiscard]] inline bool			isReady() const { return m_resourcesAllocated; }
    [[nodiscard]] inline unsigned short getNrAudioChannels() const{ return m_audioNumChannels; }
    [[nodiscard]] inline unsigned short getNrVideoTracks() const{ return m_videoNrTracks; }

    bool                                setAudioConverter(int destSampleRate, AVSampleFormat format);
    inline void					        setFirstVideoFrameCb(std::function<void()> cbFunc) { m_firstVideoFrameCb = std::move(cbFunc); }
    inline void					        setFirstAudioFrameCb(std::function<void()> cbFunc) { m_firstAudioFrameCb = std::move(cbFunc); }
    inline void					        setEndCbFunc(std::function<void()> cbFunc) { m_endCb = std::move(cbFunc); }
    inline void					        setAudioUpdtCb(std::function<void(audioCbData&)> cbFunc) { m_audioCb = std::move(cbFunc); }
    inline void					        setVideoUpdtCb(std::function<void(AVFrame*)> cbFunc) { m_videoCb = std::move(cbFunc); }
    inline void					        setVideoDecoderCb(std::function<void(uint8_t*)> cbFunc) { m_decodeCb = std::move(cbFunc); }
    inline void					        setVideoFrameBufferSize(int size) { m_videoFrameBufferSize = size; }

    virtual void                        clearResources();
    bool                                decodeYuv420OnGpu() { return m_par.decodeYuv420OnGpu; }

    double  					        getDurationSec();
    double 						        getFps();
    int64_t 					        getTotalFrames();
    [[nodiscard]] inline enum AVPixelFormat getSrcPixFmt() const { return m_srcPixFmt; }
    [[nodiscard]] inline int            getNrBufferedFrames() const { return (int)m_nrBufferedFrames; }
    int                                 getSampleRate() { return m_audioCodecCtx ? (int)m_audioCodecCtx->sample_rate : 0; }
    int                                 getVideoFrameBufferSize() { return m_videoFrameBufferSize; }
    int                                 getDecFramePtr() { return m_decFramePtr; }
    int                                 getUplFramePtr() { return m_frameToUpload; }
    int                                 getFrameRateD() { return m_formatContext->streams[m_videoStreamIndex]->r_frame_rate.den; }
    int                                 getFrameRateN() { return m_formatContext->streams[m_videoStreamIndex]->r_frame_rate.num; }
    [[nodiscard]] inline uint32_t		getBitCount() const { return m_bitCount;  }

protected:
    void            initFFMpeg();
    void            setupHwDecoding();
    int				initHwDecode(AVCodecContext* ctx, const enum AVHWDeviceType type);
    virtual void    setDefaultHwDevice();
    void            allocFormatContext();
    void            checkHwDeviceType();
    void            checkForNetworkSrc(const ffmpeg::DecodePar& p);
    bool            setupStreams(const AVInputFormat*, AVDictionary**, ffmpeg::DecodePar& p);
    void            initStreamInfo();
    void            parseSeeking();
    virtual void    parseVideoCodecPar(int32_t i, AVCodecParameters* p, const AVCodec*);
    virtual void    parseAudioCodecPar(int32_t i, AVCodecParameters* p, const AVCodec*);
    virtual void    allocateResources(ffmpeg::DecodePar& p);

    static AVFrame*	allocPicture(enum AVPixelFormat pix_fmt, int width, int height, std::vector<std::vector<uint8_t>>::iterator buf);
    uint8_t*        reqNextBuf();

    void 			singleThreadDecodeLoop();
    int 			decodeVideoPacket(AVPacket* packet, AVCodecContext* codecContext);
    virtual int32_t sendPacket(AVPacket* packet, AVCodecContext* codecContext);
    virtual int32_t checkReceiveFrame(AVCodecContext* codecContext);
    int32_t         parseReceivedFrame(AVCodecContext* codecContext);
    virtual void    transferFromHwToCpu();
    int32_t         convertFrameToCpuFormat(AVCodecContext* codecContext);
    int 			decodeAudioPacket(AVPacket* packet, AVCodecContext* codecContext);

    ffmpeg::DecodePar                   m_par;
    const AVCodec*      		        m_audioCodec=nullptr;
    AVCodecContext*				        m_videoCodecCtx=nullptr;
    AVCodecContext*                     m_audioCodecCtx=nullptr;
    AVFormatContext*			        m_formatContext=nullptr;
    AVDictionary*                       m_formatOpts=nullptr;
    AVDictionary*                       m_codecOpts=nullptr;

    AVFrame*					        m_frame = nullptr;
    AVFrame*					        m_audioFrame = nullptr;
    std::vector<AVFrame*>		        m_framePtr;
    std::vector<AVFrame*>		        m_bgraFrame;
    AVPacket*					        m_packet=nullptr;
    struct SwsContext*			        m_imgConvertCtx=nullptr;
    int                                 m_decFramePtr=0;

    enum AVHWDeviceType 		        m_hwDeviceType{};
    AVBufferRef*				        m_hwDeviceCtx = nullptr;
    int64_t                             m_dstChannelLayout=0;
    enum AVSampleFormat                 m_dstSampleFmt{};
    struct SwrContext*                  m_audioSwrCtx=nullptr;


    const char*                         m_videoCodecName = nullptr;
    std::string                         m_defaultHwDevType;

    AVCodecID					        m_forceAudioCodec{};
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
    std::function<void(uint8_t*)>       m_downFrameCb;

    bool						        m_useAudioConversion=false;
    bool						        m_resourcesAllocated=false;
    bool						        m_firstFramePresented=false;
    bool						        m_loop=true;
    bool   		                        m_run=false;
    bool   		                        m_pause=false;
    bool						        m_isStream=false;
    bool						        m_hasNoTimeStamp=false;
    bool						        m_consumeFrames =false;
    bool						        m_gotFirstVideoFrame =false;
    bool						        m_gotFirstAudioFrame =false;

    int							        m_logLevel=AV_LOG_INFO;
    int 						        m_videoStreamIndex=0;
    int                                 m_videoNrTracks=0;
    int 						        m_audioStreamIndex=0;
    int 						        m_audioNumChannels=0;
    int 						        m_dstSampleRate=0;
    int 						        m_dstNumSamples=0;
    int 						        m_maxDstNumSamples=0;
    int 						        m_dstAudioNumChannels=0;
    int 						        m_dstAudioLineSize=0;
    int                                 m_scanAllPmtsSet = 0;
    int                                 m_seekByBytes = -1;
    int                                 m_genpts=0;
    int                                 m_fps = 0;
    int 						        m_srcWidth=0;
    int 						        m_srcHeight=0;

    enum AVPixelFormat			        m_srcPixFmt{};
    enum AVPixelFormat			        m_destPixFmt = AV_PIX_FMT_BGRA;
    enum AVPixelFormat			        m_hwPixFmt{};

    int64_t                             m_start_time = AV_NOPTS_VALUE;

    unsigned int				        m_nrTexBuffers=0;
    std::atomic<uint32_t>               m_nrBufferedFrames=0;
    uint32_t					        m_videoFrameBufferSize=32;
    uint32_t 				            m_nrFramesToStart=2;

    unsigned int				        m_nrPboBufs=3;
    unsigned int 				        m_actFrameNr=0;
    int 				                m_frameToUpload=-1;

    uint8_t**                           m_dstSampleBuffer=nullptr;

    double 						        m_startTime=0.0;
    double 						        m_epsZero=0.000025;
    double						        m_timeBaseDiv=0.0;
    double						        m_frameDur=0.0;

    std::vector<double>				    m_ptss;
    double						        m_lastPtss=-1.0;

    unsigned int				        m_totNumFrames=0;
    std::vector<std::vector<uint8_t>>	m_buffer;
    std::vector<uint8_t>                m_memInputBuf;
    size_t                              m_avioCtxBufferSize=4096;
    AVIOContext*                        m_ioContext=nullptr;
    ffmpeg::memin_buffer_data           m_meminBuffer;

    uint32_t					        m_bitCount=8;
    audioCbData					        m_audioCbData;
    uint64_t                            m_videoStartPts=0;
};

}

#endif