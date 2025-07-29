#pragma once

#ifndef __ANDROID__
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <atomic>
#include <condition_variable>

#include <GLBase.h>
#include "GLResNDI.h"

#include <Processing.NDI.Lib.h>

#include <ThreadedTasks/Cycler.h>

namespace ara::av::NDI {

class Source {
public:
    Source(std::string& uname, std::string& ipaddr) : m_uName(uname), m_IPAddr(ipaddr) {}
    Source(const NDIlib_source_t* ndi_src);

    void						SetName(std::string& name){m_uName=name;}
    void						SetIPAddr(std::string& ipaddr){m_IPAddr=ipaddr;}
    void						SetUrl(std::string&& url){m_Url=url;}
    void						SetUrl(std::string& url){m_Url=url;}
    void						SetNDISrc(const NDIlib_source_t* src){m_ndi_src=src;}

    [[nodiscard]] std::string	GetName() const { return m_uName; }
    [[nodiscard]] std::string	GetIPAddr() const { return m_IPAddr; }
    [[nodiscard]] std::string   GetUrl() const { return m_Url; }
    const NDIlib_source_t*		GetNDISrc() { return m_ndi_src; }

    friend bool					operator==(const Source& s, const Source& d);

private:
    std::string					m_uName{0};
    std::string					m_IPAddr{0};
    std::string					m_Url{0};
    const NDIlib_source_t*      m_ndi_src{nullptr};
};

class SourceScanner : public Cycler {
public:
    size_t									GetCount();
    Source									Get(size_t index);
    bool                                    Start(char* chanName, char* ip);

    Source									operator()(size_t index) { return Get(index); }

    std::function<void(Source*)>            m_newSourceCB;
    std::string                             m_searchStr;
    std::string                             m_searchIp;

private:
    std::vector<Source>						m_Sources;
    uint32_t								m_WaitForSourcesTimeOut_ms = 500;

protected:
    bool							        OnCycle() override;
};

class Client : public Cycler {
public:
    Client(std::string& uname, std::string& ipaddr);
    Client(Source& src);
    ~Client();

    bool		Set(std::string& uname, std::string& ipaddr);
    void		SetColorFormat(NDIlib_recv_color_format_e tp) { m_colorFormat = tp; }
    void		SetNewFrameCb(std::function<void()> f) { m_newFrameCb = std::move(f); }

    uint32_t	GetFrameWaitTimeOut() const { return m_FrameWaitTimeOut_ms; }
    bool		SetFrameWaitTimeOut(uint32_t timeout_ms) { return (m_FrameWaitTimeOut_ms = timeout_ms); }

    float		GetVideoStat_FPS() const { return m_VideoStat_FPS; }
    float		GetVideo_ExpectedFPS() const { return m_Video_ExpectedFPS; }

    // int		GetFramewidth() { return m_frameWidth.load(); }
    // int		GetFrameHeight() { return m_frameHeight.load(); }

    std::string GetName() { return m_source.GetName(); }

    bool		HasVideoSignal() { return m_VideoStat_LastFrameOK; }
    bool		HasStableVideoSignal(float max_delta = 1) { return (fabsf(m_VideoStat_FPS - m_Video_ExpectedFPS) <= max_delta); }

    bool		opt_SetAccel(bool on_off);
    bool		opt_SetTally(bool program, bool preview);

    double		GetSecondsSinceLastFrame();

private:
    Source						m_source;
    NDIlib_source_t				ndiSrc;
    NDIlib_recv_instance_t		m_NDI_Recv = nullptr;

    uint32_t					m_FrameWaitTimeOut_ms = 5000;

    float						m_VideoStat_FPS = 0;
    float						m_VideoStat_Period_ms = 500;
    bool						m_VideoStat_LastFrameOK = 0;

    float						m_Video_ExpectedFPS = 0;

    std::atomic<int>			m_frameWidth=0;
    std::atomic<int>			m_frameHeight=0;

    NDIlib_recv_color_format_e  m_colorFormat=NDIlib_recv_color_format_fastest;

    bool						ProcessVideoFrame(NDIlib_video_frame_v2_t* vframe);

    std::chrono::time_point<std::chrono::system_clock> m_FrameLastTime;				// Last time in seconds since the last m_frame arrived
    std::function<void()>       m_newFrameCb;

protected:
    virtual bool    OnCycle();
    virtual bool	OnFrameArrive(NDIlib_video_frame_v2_t* vframe) { return false; }
    void			ResetTimeReference();			// sets m_FrameLastTime to now - 100 seconds
};

class BufferClient : public Client {
public:
    class CIBuff {
    public:
        std::vector<uint8_t>				img;
        int									pixSize[2] = { 0,0 };
        int									stride = 0;
        NDIlib_FourCC_video_type_e			fourCC = NDIlib_FourCC_video_type_UYVY;
        bool								Feed(NDIlib_video_frame_v2_t* vframe);
    };

    BufferClient(std::string& uname, std::string ipaddr);
    BufferClient(Source& src);

    CIBuff* GetLastBuff();
protected:
    virtual bool OnFrameArrive(NDIlib_video_frame_v2_t* vframe);

    CIBuff*	m_lastUplBuf = nullptr;

private:
    bool AllocateIBuffers();

    std::vector<std::unique_ptr<CIBuff>>	iBuff;
    int										iBuffPos = 0;
    int										iBuffLastPos = -1;
};

}

#endif