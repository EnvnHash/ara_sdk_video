//
// Created by user on 08.12.2021.
//

#pragma once

#ifdef ARA_USE_OPENAL

#ifdef _WIN32

#ifdef VIDEO_EXPORT
#define VIDEO_BIND __declspec(dllexport)
#else
#define VIDEO_BIND __declspec(dllimport)
#endif

#else
#define VIDEO_BIND
#endif

#ifdef __ANDROID__
#define AL_LIBTYPE_STATIC
#endif

#include <util_common.h>
#include <AVCommon.h>
#include <Log.h>
#include <Conditional.h>
#ifdef __linux__
#include <AL/al.h>
#include <AL/alc.h>
#else
#include <al.h>
#include <alc.h>
#endif
#include <CycleBuffer.h>

namespace ara::av
{

class OpenALSource
{
public:
    OpenALSource() { m_audioMtx = new std::mutex(); };
    virtual ~OpenALSource();

    void init();
    void recv_audio_packet(audioCbData& data);
    void proc();
    void stop();

    inline bool isPlaying()                             { return m_playing; }
    inline void setFeedBlock(std::atomic<bool>* flg)    { m_feedBlock=flg; }
    inline void setPosition(float x, float y, float z)  { if (m_source) alSource3f(m_source, AL_POSITION, x, y, z); }

    ALuint*             m_buffers=nullptr;
    ALint               buffersProcessed = 0;
    ALuint              m_source=0;
    ALfloat             m_vol=1.f;
    std::mutex*         m_audioMtx=nullptr;
    std::thread         m_procThread;
    Conditional         m_exitSema;
    int                 m_num_al_buffers=128;

    int                 iState = 0;
    int                 m_nrChannels=0;
    int                 m_nrSamples=0;
    int                 m_bufSize=0;
    int                 m_sampleRate=0;

    int                 m_queuePtr=0;
    size_t              m_writePtr=0;
    size_t              m_inputQueueSize=0;
    bool                m_queueFilled=false;
    bool                m_inited=false;
    bool                m_playing=false;
    std::atomic<bool>   m_play=true;
    std::atomic<bool>*  m_feedBlock=nullptr;
};

class OpenAL {
public:
    OpenAL() = default;
    ~OpenAL() = default;

    void m_init();
    OpenALSource* m_addSource();
    static void list_audio_devices(const ALCchar *devices);
    static int test_error(const char* msg);
    void destroy();

    void setDistModel(int model)                        { alDistanceModel(AL_DISTANCE_MODEL + model); }
    void setListenerPos(float x, float y, float z)      { alListener3f(AL_POSITION, x, y, z); }
    void setListenerOriAt(float x, float y, float z)    {
        m_listenerOri[0] = x; m_listenerOri[1] = y; m_listenerOri[2] = z;
        m_listenerOri[3] = 0.0f; m_listenerOri[4] = 1.0f; m_listenerOri[5] = 0.0f;
        alListenerfv(AL_ORIENTATION, m_listenerOri);
    }

    std::atomic<bool>                       m_inited=false;

private:
    static inline ALCdevice*                m_dev=nullptr;
    static inline ALCcontext*               m_ctx=nullptr;
    const ALCchar*                          m_defaultDeviceName=nullptr;
    ALfloat                                 m_listenerOri[6]={0.f};
    ALfloat                                 m_listenerPos[3]={0};
    ALint                                   m_distModel = 0;

    std::vector<std::unique_ptr<OpenALSource>>  m_sources;
};

}

#endif