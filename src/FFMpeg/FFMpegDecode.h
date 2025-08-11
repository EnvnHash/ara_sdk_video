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
#include <CycleBuffer.h>

namespace ara::av {

class FFMpegDecode {
public:
    virtual void openFile(const ffmpeg::DecodePar& p);
    virtual void openCamera(const ffmpeg::DecodePar& p);
    virtual void start(double time);
    virtual void stop();
    void 		 setPause(bool val) { m_pause = val; }

    // 2 thread implementation of sending/receiving decoded frames, .... not much faster than the "while" variant
    //void 						        sendFrameLoop();
    //int 						        receiveFrameLoop();

    uint8_t*    reqNextBuf();
    void 		seekFrame(int64_t frame_number, double time);
    void 		seek(double sec, double time) { seekFrame((int64_t) (sec * getFps(ffmpeg::streamType::video) + 0.5), time); }
    void		resetToStart(double time) { seek(0.0, time); }

    [[nodiscard]] bool			    isRunning() const { return m_run; }
    [[nodiscard]] bool			    isReady() const { return m_resourcesAllocated; }
    [[nodiscard]] unsigned short    getNrAudioChannels() const{ return m_audioNumChannels; }
    [[nodiscard]] unsigned short    getNrVideoTracks() const{ return m_videoNrTracks; }

    bool setAudioConverter(int destSampleRate, AVSampleFormat format);
    void setFirstVideoFrameCb(std::function<void()> cbFunc) { m_firstVideoFrameCb = std::move(cbFunc); }
    void setFirstAudioFrameCb(std::function<void()> cbFunc) { m_firstAudioFrameCb = std::move(cbFunc); }
    void setEndCbFunc(std::function<void()> cbFunc) { m_endCb = std::move(cbFunc); }
    void setAudioUpdtCb(std::function<void(audioCbData&)> cbFunc) { m_audioCb = std::move(cbFunc); }
    void setVideoUpdtCb(std::function<void(AVFrame*)> cbFunc) { m_videoCb = std::move(cbFunc); }
    void setVideoDecoderCb(std::function<void(uint8_t*)> cbFunc) { m_decodeCb = std::move(cbFunc); }
    void setVideoFrameBufferSize(int size) { m_videoFrameBufferSize = size; }

    virtual void clearResources();
    bool         decodeYuv420OnGpu() { return m_par.decodeYuv420OnGpu; }

    double  					        getDurationSec(ffmpeg::streamType);
    double 						        getFps(ffmpeg::streamType);
    int64_t 					        getTotalFrames(ffmpeg::streamType);
    [[nodiscard]] enum AVPixelFormat    getSrcPixFmt() const { return m_srcPixFmt; }
    [[nodiscard]] int                   getNrBufferedFrames() { return m_frames.getFillAmt(); }
    int                                 getSampleRate() { return m_audioCodecCtx ? (int)m_audioCodecCtx->sample_rate : 0; }
    int                                 getVideoFrameBufferSize() { return m_videoFrameBufferSize; }
    int                                 getWriteFramePtr() { return m_frames.getWritePos(); } // m_frames write buff
    int                                 getReadFramePtr() { return m_frames.getReadPos(); }
    int                                 getFrameRateD() { return m_formatContext->streams[toType(ffmpeg::streamType::video)]->r_frame_rate.den; }
    int                                 getFrameRateN() { return m_formatContext->streams[m_streamIndex[toType(ffmpeg::streamType::video)]]->r_frame_rate.num; }
    [[nodiscard]] uint32_t		        getBitCount() const { return m_bitCount; }
    [[nodiscard]] auto&		            getDecodeCond() { return m_decodeCond; }

protected:
    static void     initFFMpeg();
    void            setupHwDecoding();
    int				initHwDecode(AVCodecContext* ctx, const enum AVHWDeviceType type);
    virtual void    setDefaultHwDevice();
    void            allocFormatContext();
    void            checkHwDeviceType();
    void            checkForNetworkSrc(const ffmpeg::DecodePar& p);
    bool            setupStreams(const AVInputFormat*, AVDictionary**, ffmpeg::DecodePar& p);
    void            setStreamTiming(int32_t i, ffmpeg::streamType t);
    void            initStreamInfo();
    void            parseSeeking();
    virtual void    parseVideoCodecPar(int32_t i, AVCodecParameters* p, const AVCodec*);
    virtual void    parseAudioCodecPar(int32_t i, AVCodecParameters* p, const AVCodec*);
    virtual void    allocateResources(ffmpeg::DecodePar& p);

    static AVFrame*	allocPicture(enum AVPixelFormat pix_fmt, int width, int height, std::vector<uint8_t>& buf);

    void 			singleThreadDecodeLoop();
    int 			decodeVideoPacket(AVPacket* packet, AVCodecContext* codecContext);
    virtual int32_t sendPacket(AVPacket* packet, AVCodecContext* codecContext);
    virtual int32_t checkReceiveFrame(AVCodecContext* codecContext);
    int32_t         parseReceivedFrame(AVCodecContext* codecContext);
    void            incrementWritePos();
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
    CycleBuffer<ffmpeg::TimedFrame>     m_frames;
    CycleBuffer<ffmpeg::TimedPicture>   m_bgraFrame;
    AVPacket*					        m_packet=nullptr;
    struct SwsContext*			        m_imgConvertCtx=nullptr;

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
    bool   		                        m_run=false;
    bool   		                        m_pause=false;
    bool						        m_isStream=false;
    bool						        m_hasNoTimeStamp=false;
    bool						        m_consumeFrames =false;
    bool						        m_gotFirstVideoFrame =false;
    bool						        m_gotFirstAudioFrame =false;

    int							        m_logLevel=AV_LOG_INFO;
    int                                 m_videoNrTracks=0;
    int 						        m_audioNumChannels=0;
    int 						        m_dstSampleRate=0;
    int 						        m_dstNumSamples=0;
    int 						        m_dstAudioNumChannels=0;
    int 						        m_dstAudioLineSize=0;
    int                                 m_scanAllPmtsSet = 0;
    int                                 m_seekByBytes = -1;
    int                                 m_genpts=0;
    int 						        m_srcWidth=0;
    int 						        m_srcHeight=0;

    std::array<int32_t, 2>              m_fps{};
    std::array<int32_t, 2>		        m_streamIndex{};

    enum AVPixelFormat			        m_srcPixFmt{};
    enum AVPixelFormat			        m_destPixFmt = AV_PIX_FMT_BGRA;
    enum AVPixelFormat			        m_hwPixFmt{};

    int64_t                             m_start_time = AV_NOPTS_VALUE;

    unsigned int				        m_nrTexBuffers=0;
    uint32_t					        m_videoFrameBufferSize=32;
    uint32_t 				            m_nrFramesToStart=2;

    unsigned int				        m_nrPboBufs=3;
    unsigned int 				        m_actFrameNr=0;

    uint8_t**                           m_dstSampleBuffer=nullptr;

    double 						        m_startTime=0.0;
    double 						        m_epsZero=0.000025;
    double					            m_audioToVideoDurationDiff = 0.0;

    std::array<double, 2>		        m_timeBaseDiv{};
    std::array<double, 2>		        m_frameDur{};
    std::array<double, 2>		        m_streamDuration{}; // should be the same
    std::array<double, 2>		        m_lastPtss{-1.0, -1.0};
    int 				                m_frameToUpload=-1;

    unsigned int				        m_totNumFrames=0;
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