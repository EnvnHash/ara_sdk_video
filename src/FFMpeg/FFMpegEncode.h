//
// Created by user on 11.08.2021.
//

#pragma once

#ifdef ARA_USE_FFMPEG

#include <FFMpeg/FFMpegCommon.h>
#include <StopWatch.h>
#include <libyuv.h>

namespace ara::av {

class FFMpegEncode {
public:
    // a wrapper around a single output AVStream
    class OutputStream {
    public:
        AVStream *st;
        AVCodecContext *enc;

        /* pts of the next m_frame that will be generated */
        int64_t next_pts;
        int samples_count;

        AVFrame *frame;
        AVFrame *tmp_frame;

        float t;

        struct SwsContext *sws_ctx;
        struct SwrContext *swr_ctx;
    };

    class RecFrame {
    public:
        std::vector<uint8_t>    buffer;
        uint8_t* 				bufferPtr=nullptr;
        int64_t                 pts;
        double                  encTime; // ms
    };

    FFMpegEncode() = default;
    FFMpegEncode(const char* fname, int inWidth, int inHeight, int fps, AVPixelFormat inpPixFmt, bool hwEncode=false);
    virtual ~FFMpegEncode();

    bool init(const char* fname, int inWidth, int inHeight, int fps, AVPixelFormat inpPixFmt, bool hwEncode=false);
    bool record();
    inline void stop() { std::unique_lock<std::mutex> l(m_writeMtx); m_doRec = false; m_stopCond.wait(0); }

    virtual void recThread();
    int         set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx);

    void        log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt);
    int         write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);
    void        add_stream(FFMpegEncode::OutputStream *ost, AVFormatContext *oc, AVCodec **codec, enum AVCodecID codec_id);
    AVFrame*    alloc_audio_frame(enum AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples);
    void        open_audio(AVCodec *codec, FFMpegEncode::OutputStream *ost, AVDictionary *opt_arg);
    AVFrame*    get_audio_frame(OutputStream *ost, bool clear);
    int         write_audio_frame(AVFormatContext *oc, FFMpegEncode::OutputStream *ost,  bool clear);
    void        open_video(AVCodec *codec, FFMpegEncode::OutputStream *ost, AVDictionary *opt_arg);
    AVFrame*    alloc_picture(enum AVPixelFormat pix_fmt, int width, int height);
    int         write_video_frame(AVFormatContext *oc, FFMpegEncode::OutputStream *ost);
    void        close_stream(AVFormatContext *oc, FFMpegEncode::OutputStream *ost);

    void        downloadGlFbToVideoFrame(double fixFps=0.0, unsigned char* bufPtr=nullptr);
    //void        downloadGlFbToVideoFrame(unsigned char* bufPtr);

   /* static void setAudioDataCallback(unsigned int numChans, int frames, int offset,
    			int codecFrameSize, unsigned int codecNrChans, std::vector< std::vector<unsigned int> >* m_mixDownMap,
    			float** sampData, std::vector<float>* inSampQ);*/
    //void mediaRecAudioDataCallback(PAudio::paPostProcData* paData);

    void        freeGlResources();

    bool isRecording() const                        { return m_doRec; }
    bool isInited() const                           { return m_inited; }
    void setVideoBitRate(unsigned int bitRate)      { m_videoBitRate = bitRate; }
    void setAudioBitRate(unsigned int bitRate)      { m_audioBitRate = bitRate; }
    void setHFlip(bool val)                         { m_flipH = val; }
    void setBufOvrCb(std::function<void(bool)> f)   { m_bufOvrCb = std::move(f); }

private:
#ifdef WITH_AUDIO
    PAudio* 				pa;
    PAudio::paPostProcData	paData;
#endif
    std::string				    m_fileName;
    std::string				    m_fileType;
    std::string				    m_forceCodec={0};

    AVDictionary*			    m_opt=nullptr;
    AVOutputFormat*			    m_fmt=nullptr;
    AVFormatContext*		    m_oc=nullptr;
    AVCodecContext*             m_encCodecCtx=nullptr;
    AVCodec*				    m_audio_codec=nullptr;
    AVCodec*				    m_video_codec=nullptr;
    AVBufferRef*                m_hw_device_ctx=nullptr;
    AVPixelFormat               m_hwPixFmt=(AVPixelFormat)0;
    AVPixelFormat               m_hwSwFmt=(AVPixelFormat)0;
    AVPacket                    m_decPkt;
    AVFrame*                    m_encFrame=nullptr;
    AVFrame*                    m_frameBGRA=nullptr;

    AVFilterGraph*			    m_filter_graph=nullptr;
    AVFilterContext*		    m_buffersink_ctx=nullptr;
    AVFilterContext*		    m_buffersrc_ctx=nullptr;
    SwsContext*				    m_converter=nullptr;	// muss unbedingt auf NULL initialisiert werden!!!

    uint8_t**				    m_src_samples_data=nullptr;
    int       				    m_src_samples_linesize;
    int       				    m_src_nb_samples;
    int 					    m_max_dst_nb_samples;
    uint8_t**				    m_dst_samples_data=nullptr;
    int       				    m_dst_samples_linesize;
    int       				    m_dst_samples_size;
    struct SwrContext*		    m_swr_ctx = nullptr;
    std::vector<float> 		    m_audioQueue;

    AVFrame*				    m_inpFrame=nullptr;
    AVFrame*				    m_filt_frame=nullptr;
    AVFrame*				    m_hw_frame=nullptr;

    int 					    m_ret=0;
    int                         m_have_video = 0;
    int                         m_have_audio = 0;
    int                         m_encode_video = 0;
    int                         m_encode_audio = 0;
    int						    m_bufferReadPtr=0;
    int						    m_fps=0;

    int			 			    m_width=0;
    int 					    m_height=0;

    int						    m_audioBitRate=64000;
    int						    m_videoBitRate=800000;
  //  int					    	m_videoBitRate=400000;

    unsigned int			    m_nrBufferFrames;
    unsigned int			    m_writeVideoFrame = 0;
    unsigned int			    m_nrReadVideoFrames = 0;
    unsigned int 			    m_glNrBytesPerPixel=3;

    AVPixelFormat			    m_inpPixFmt;

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
    bool					    m_hwEncode = false;
    bool					    m_gotFirstFrame = false;
    bool					    m_av_interleaved_wrote_first = false;
    std::atomic<bool>		    m_implictGlFbDownload = false;

    double                      m_timeBaseMult=1.0;
    double                      m_frameDur=0.0;
    double                      m_encTimeDiff=0.0;
    double                      m_encElapsedTime=0.0;

    std::thread*	 		    m_Thread=nullptr;
    std::thread	 		        m_glDownThread;
    std::thread	 		        m_saveThread;

    //--------------------------------------------------------

    FFMpegEncode::OutputStream  n_video_st;
    FFMpegEncode::OutputStream  n_audio_st;

    std::vector< std::vector<unsigned int> > m_mixDownMap;
    unsigned int 			    m_nrMixChans=0;
    unsigned char* 			    m_ptr=nullptr;

    GLenum 					    m_glDownloadFmt=0;
    std::vector<GLuint>         m_pbos;
    size_t 				        m_num_pbos=0;
    size_t 				        m_dx=0;
    size_t 				        m_num_filled_pbos=0;

    std::string				    m_errStr;
    std::string				    m_rtmpUrl;

    int 					    m_nbytes; // number of bytes in the pbo m_buffer.
    int                         m_stride[8];
    int                         m_sws_flags = SWS_BILINEAR | SWS_ACCURATE_RND;

    int                         m_fakeCntr=0;

#if defined(ARA_USE_LIBRTMP) && defined(_WIN32)
    util::RTMPSender            m_rtmpSender;
#endif

    std::vector<RecFrame>       m_recQueue;

    StopWatch                   m_scaleTime;
    Conditional                 m_recCond;
    Conditional                 m_stopCond;

    std::mutex                  m_writeMtx;

    std::chrono::system_clock::time_point   m_lastEncTime{};
    std::chrono::system_clock::time_point   m_startEncTime{};
    std::chrono::system_clock::time_point   m_now{};
    std::chrono::system_clock::time_point   m_lastBufOvrTime{};
    std::function<void(bool)>   m_bufOvrCb;

    std::filesystem::path       m_downloadFolder;
    std::unique_ptr<Texture>    m_downTex;
    int                         m_pngSeqCnt=0;
    std::mutex                  m_saveQueueMtx;
    std::array<std::function<void()>, 64> m_saveQueue;
    int                         m_saveQueueRead=0;
    int                         m_saveQueueWrite=0;
    int                         m_saveQueueSize=0;
};

}

#endif