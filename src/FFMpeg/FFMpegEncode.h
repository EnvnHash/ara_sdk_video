//
// Created by user on 11.08.2021.
//

#pragma once

#ifdef ARA_USE_FFMPEG

#include <FFMpeg/FFMpegCommon.h>
#include <StopWatch.h>
#include <CycleBuffer.h>
#include <libyuv.h>

namespace ara::av {

class FFMpegEncode {
public:
    // a wrapper around a single output AVStream
    struct OutputStream {
        AVStream *st{};
        AVCodecContext *enc{};
        const AVCodec* codec{};
        AVCodecID codecID{};

        int64_t next_pts{}; // pts of the next m_frame that will be generated
        int32_t samples_count{};

        AVFrame *frame{};
        AVFrame *tmp_frame{};

        float t{};

        SwsContext *sws_ctx{};
        SwrContext *swr_ctx{};
    };

    FFMpegEncode() = default;
    explicit FFMpegEncode(const ffmpeg::EncodePar& par);
    virtual ~FFMpegEncode() = default;

    bool init(const ffmpeg::EncodePar& par);
    bool record(double time=0.0);
    void openOutputFile(AVDictionary *avioOpts);
    void stop() { m_doRec = false; m_stopCond.wait(0); }

    int         writePacket(AVFormatContext *fmt_ctx, OutputStream *ost, AVPacket *pkt);
    void        addStream(OutputStream *ost, AVFormatContext *oc);
    void        openAudio(OutputStream *ost, AVDictionary *opt_arg);
    AVFrame*    getAudioFrame(OutputStream *ost, bool clear);
    int         writeAudioFrame(AVFormatContext *oc, OutputStream *ost, bool clear);
    void        openVideo(OutputStream *ost, AVDictionary *opt_arg);
    int         writeVideoFrame(AVFormatContext *oc, OutputStream *ost);
    void        closeStream(AVFormatContext *oc, OutputStream *ost);

    void        downloadGlFbToVideoFrame(double fixFps=0.0, unsigned char* bufPtr=nullptr, bool monotonic=false, int64_t pts=-1);
    void        freeGlResources();

    [[nodiscard]] bool isRecording() const          { return m_doRec; }
    [[nodiscard]] bool isInited() const             { return m_inited; }
    void setHFlip(bool val)                         { m_flipH = val; }
    void setBufOvrCb(std::function<void(bool)> f)   { m_bufOvrCb = std::move(f); }

protected:
    void            setupHardwareContext();
    void            checkRtmpRequested(AVDictionary *avioOpts);
    void            allocFormatCtx();
    void            addStreams();
    void            openStreams();
    virtual void    recThread();
    void            cleanUp();
    int             setHwframeCtx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx);
    void            savePngSeq();
    void            setVideoCodePar(OutputStream *ost);

#ifdef WITH_AUDIO
    PAudio* 				pa;
    PAudio::paPostProcData	paData;
#endif
    ffmpeg::EncodePar               m_par{};
    std::string				        m_fileType;
    std::string				        m_forceCodec;
    AVDictionary*			        m_opt=nullptr;
    const AVOutputFormat*		    m_fmt=nullptr;
    AVFormatContext*		        m_oc=nullptr;
    AVBufferRef*                    m_hw_device_ctx=nullptr;
    AVPixelFormat                   m_hwPixFmt{};
    AVPixelFormat                   m_hwSwFmt{};
    AVFrame*                        m_encFrame=nullptr;
    AVFrame*                        m_frameBGRA=nullptr;
    AVFilterContext*		        m_buffersink_ctx=nullptr;
    AVFilterContext*		        m_buffersrc_ctx=nullptr;
    SwsContext*				        m_converter=nullptr;	// muss unbedingt auf NULL initialisiert werden!!!

    int64_t m_lastPts{};
    int64_t m_lastPktPts{};

    uint8_t**				    m_src_samples_data=nullptr;
    int       				    m_src_samples_linesize{};
    int       				    m_src_nb_samples{};
    int 					    m_max_dst_nb_samples{};
    uint8_t**				    m_dst_samples_data=nullptr;
    int       				    m_dst_samples_linesize{};
    int       				    m_dst_samples_size{};
    std::vector<float> 		    m_audioQueue;
    AVFrame*				    m_inpFrame=nullptr;
    AVFrame*				    m_filt_frame=nullptr;
    AVFrame*				    m_hw_frame=nullptr;
    int 					    m_ret=0;
    std::array<bool, 2>         m_have{};
    unsigned int			    m_nrBufferFrames = 16;
    int32_t 			        m_glNrBytesPerPixel = 3;
    bool					    m_savePngFirstCall = true;
    bool					    m_savePngSeq = false;
    bool					    m_doRec = false;
    bool					    m_bufOvr = false;
    bool					    m_useFiltering = false;
    bool					    m_flipH = true;
    bool					    m_inited = false;
    bool					    m_noAudio = true;
    bool					    m_is_net_stream = false;
    bool					    m_isRtmp = false;
    bool					    m_gotFirstFrame = false;
    bool					    m_av_interleaved_wrote_first = false;
    double                      m_frameDur=0.0;
    double                      m_encTimeDiff=0.0;
    double                      m_encElapsedTime=0.0;
    std::thread*	 		    m_Thread=nullptr;
    std::thread	 		        m_glDownThread;
    std::thread	 		        m_saveThread;
    std::array<OutputStream, 2> m_outStream;

    std::vector< std::vector<unsigned int> > m_mixDownMap;
    unsigned int 			    m_nrMixChans=0;

    CycleBuffer<ffmpeg::RecFrame> m_videoFrames;

    GLenum 					    m_glDownloadFmt{};
    CycleBuffer<GLuint>         m_pbos;
    static inline size_t 		m_num_pbos=4;
    std::string				    m_errStr;
    std::string				    m_rtmpUrl;
    int 					    m_nbytes{}; // number of bytes in the pbo m_buffer.
    int                         m_stride[8]{};
    int                         m_sws_flags = SWS_BILINEAR | SWS_ACCURATE_RND;
    int                         m_fakeCntr=0;
#if defined(ARA_USE_LIBRTMP) && defined(_WIN32)
    util::RTMPSender            m_rtmpSender;
#endif
    StopWatch                   m_scaleTime;
    Conditional                 m_recCond;
    Conditional                 m_stopCond;
    std::function<void(bool)>   m_bufOvrCb;
    std::unique_ptr<Texture>    m_downTex;
    int                         m_pngSeqCnt=0;
    std::array<std::function<void()>, 64> m_saveQueue;
    int                         m_saveQueueRead=0;
    int                         m_saveQueueWrite=0;
    int                         m_saveQueueSize=0;

    std::chrono::system_clock::time_point   m_lastEncTime{};
    std::chrono::system_clock::time_point   m_startEncTime{};
    std::chrono::system_clock::time_point   m_now{};
    std::chrono::system_clock::time_point   m_lastBufOvrTime{};
};

}

#endif