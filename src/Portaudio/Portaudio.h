/*
 *  Portaudio.h
 *
 *  Created by Sven Hahne on 27.07.20.
 *
 */

#pragma once

#ifdef ARA_USE_PORTAUDIO

#include <portaudio.h>
#include <AVCommon.h>
#include <Conditional.h>
#include <CycleBuffer.h>

#ifdef __APPLE__
#include <pa_mac_core.h>
#elif __linux__
//#include <pa_jack.h>
#include <pa_linux_alsa.h>
#endif

namespace ara::av {

struct PaInitPar {
    int32_t sampleRate = 0;
    int32_t numChannels = 0;
    bool useCycleBuffer = true;
};

class Portaudio {
public :
    struct paSampBuf {
        float left_phase{};
        float right_phase{};
    };

    Portaudio()=default;

    bool init(const PaInitPar& = PaInitPar());
    void start();
    void stop();
    void pause();
    void resume();

    static bool isNrOutChanSupported(int destNrChannels);
    bool isSampleRateSupported(double destSampleRate);
    static int getMaxNrOutChannels();
    static int getValidOutSampleRate(int destSampleRate);

    static int paCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData);

    static void terminate(const PaError& err) {
        Pa_Terminate();
        LOGE << "An error occured while using the portaudio stream";
        LOGE << "Error number: " << err;
        LOGE << "Error message: " << Pa_GetErrorText(err);
    }

    [[nodiscard]] bool isRunning() const            { return m_isPlaying; }
    [[nodiscard]] int getFramesPerBuffer() const    { return m_framesPerBuffer; }
    [[nodiscard]] int getNrOutChannels() const      { return m_outputParameters.channelCount; }
    auto& getCycleBuffer()      { return m_cycleBuffer; }
    auto& getStreamMtx()                  { return m_streamMtx; }
    auto& getStreamProcCb()      { return m_streamProcCb; }
    auto& useCycleBuf()                 { return m_useCycleBuf; }

    void setSampleRate(int rate)                { m_sample_rate = rate; }
    void setFramesPerBuffer(int nrFrames)       { m_framesPerBuffer = nrFrames; }
    void setNrOutputChannel(int nrChan)         { m_outputParameters.channelCount = nrChan; }
    void setFeedBlock(std::atomic<bool>* bl)    { m_feedBlock = bl; }
    void setFeedBlockMultiple(size_t mltpl)     { m_feedMultiple = mltpl; }
    void setUseCycleBuf(bool val)               { m_useCycleBuf = val; }

    void setStreamProcCb(const std::function<void(const void*, void*, uint64_t)>& f) { m_streamProcCb = f; }

    static inline PaStream* stream=nullptr;
    static paSampBuf        data;

private:
    int                     m_sample_rate=44100;
    int                     m_framesPerBuffer=256;
    bool                    m_isPlaying = false;
    bool                    m_useCycleBuf = true;
    size_t                  m_feedMultiple = 1;
    std::mutex              m_streamMtx;
    PaStreamParameters      m_inputParameters{0};
    PaStreamParameters      m_outputParameters{0};
    CycleBuffer<float>      m_cycleBuffer;
    std::atomic<bool>*      m_feedBlock=nullptr;

    std::function<void(const void*, void*, uint64_t)>   m_streamProcCb;
};
}

#endif