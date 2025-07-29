//
// Created by user on 08.12.2021.
//

#ifdef ARA_USE_OPENAL

#include "OpenAL/OpenAL.h"

using namespace std;

namespace ara::av {

void OpenALSource::init()
{
    unique_lock<mutex> l(*m_audioMtx);

    m_buffers = new ALuint[m_num_al_buffers];
    alGenBuffers(m_num_al_buffers, m_buffers);
    m_queuePtr = 0;

    alGenSources(1, &m_source);
    if (alGetError() != AL_NO_ERROR)
    {
        LOGE <<  "OpenALSource::init Error: failed to generate a new source";
        return;
    }

    alSourcef(m_source, AL_PITCH, 1.f);
    alSourcef(m_source, AL_GAIN, 0.f);
    alSource3f(m_source, AL_POSITION, 0.f, 0.f, 0.f);
    alSource3f(m_source, AL_VELOCITY, 0.f, 0.f, 0.f);
    alSourcei(m_source, AL_LOOPING, AL_FALSE);

    alSourcePlay(m_source);

    m_procThread = std::thread( [this]{
        while(m_play)
        {
            proc();
            std::this_thread::sleep_for(500us);
        }
        m_exitSema.notify();
        return;
    });

    m_procThread.detach();
    m_inited = true;
}

/** Note: nrSamples refers to a single channel */
void OpenALSource::recv_audio_packet(audioCbData& data)
{
    m_nrChannels = data.nChannels;
    m_nrSamples = data.samples;
    m_bufSize = data.byteSize;
    m_sampleRate = data.sampleRate;

    if (m_inputQueueSize == m_num_al_buffers)
    {
        LOGE << " audio queue full";
        return;
    }

    alBufferData(m_buffers[m_writePtr],
                 m_nrChannels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                 &data.buffer[0][0],
                 m_bufSize,
                 m_sampleRate);
    alSourceQueueBuffers(m_source, 1, &m_buffers[m_writePtr]);

    ++m_writePtr %= m_num_al_buffers;
    ++m_inputQueueSize;

    if (m_inputQueueSize == m_num_al_buffers && m_feedBlock)
        *m_feedBlock = true;
}

void OpenALSource::proc()
{
    if (!m_queueFilled && m_inputQueueSize < 16) // the higher, the more delay, the lower, more risk for audio queue to starve
        return;
    else
    {
        if (!m_queueFilled)
        {
            alSourcef(m_source, AL_GAIN, m_vol);
            alSourcePlay(m_source);
            m_queueFilled = true;
            m_playing = true;
        }
    }

    // Request the number of OpenAL Buffers that have been processed (played) on the Source
    alGetSourcei(m_source, AL_BUFFERS_PROCESSED, &buffersProcessed);
    //LOG << "proc buffersProcessed " << buffersProcessed;

    if (buffersProcessed == 0) return;

    // try to unqueue as many buffers as possible in one command
    // how many buffer can be unqueued from m_buffers without crossing the end?
    int nrUnqueueBufs = std::min(std::max(0, m_num_al_buffers - m_queuePtr), buffersProcessed);

    alSourceUnqueueBuffers(m_source, nrUnqueueBufs, &m_buffers[m_queuePtr]);

    // are there more buffers to free?
    nrUnqueueBufs = std::max(0, buffersProcessed - nrUnqueueBufs);
    if (nrUnqueueBufs)
        alSourceUnqueueBuffers(m_source, nrUnqueueBufs, &m_buffers[0]);

    m_queuePtr = (m_queuePtr + buffersProcessed) % m_num_al_buffers;
    //LOG << "m_queuePtr " << m_queuePtr << " nrUnqueueBufs " << nrUnqueueBufs;
    m_inputQueueSize -= buffersProcessed;

    // Check the status of the Source.  If it is not playing, then playback was completed,
    // or the Source was starved of audio data, and needs to be restarted.
    alGetSourcei(m_source, AL_SOURCE_STATE, &iState);
    if (iState != AL_PLAYING)
    {
        // If there are Buffers in the Source Queue then the Source was starved of audio
        // data, so needs to be restarted (because there is more audio data to play)
        alGetSourcei(m_source, AL_BUFFERS_QUEUED, &buffersProcessed);
        if (buffersProcessed)
        {
            alSourcef(m_source, AL_GAIN, m_vol);
            alSourcePlay(m_source);
            m_playing = true;
        }
    }

    // unblock buffer feeding, if blocked
    if (m_feedBlock && *m_feedBlock)
        *m_feedBlock = false;
}

void OpenALSource::stop()
{
    if (!m_inited) return;

    unique_lock<mutex> l(*m_audioMtx);
    m_play = false;
    m_exitSema.wait();

    if (m_source)
    {
        alSourcef(m_source, AL_GAIN, 0.f);
        alSourceStop(m_source);
        alDeleteSources(1, &m_source);
        m_source= 0;
    }

    alDeleteBuffers(m_num_al_buffers, m_buffers);
    delete [] m_buffers;
    m_buffers = nullptr;

    m_inited = false;
}

OpenALSource::~OpenALSource()
{
    if (m_buffers)
    {
        unique_lock<mutex> l(*m_audioMtx);
        alDeleteBuffers(m_num_al_buffers, m_buffers);
        delete [] m_buffers;
    }
    delete m_audioMtx;
}


/// <summary>
/// init a singleton OpenAL context
/// </summary>
void OpenAL::m_init()
{
    if (m_inited) return;

    //list_audio_devices(alcGetString(nullptr, ALC_DEVICE_SPECIFIER));

    if (!m_defaultDeviceName)
        m_defaultDeviceName = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);

    m_dev = alcOpenDevice(nullptr);
    if(!m_dev) {
        LOGE << "alcOpenDevice failed";
        return;
    }

    LOG << "OpenAL opening Device: " << alcGetString(m_dev, ALC_DEVICE_SPECIFIER);

    m_ctx = alcCreateContext(m_dev, nullptr);
    alcMakeContextCurrent(m_ctx);
    if(!m_ctx) {
        LOGE << "alcMakeContextCurrent failed";
        return;
    }

    test_error("OpenAL init");

    ALCint maj=0, min=0;
    alcGetIntegerv(m_dev, ALC_MAJOR_VERSION, 1, &maj);
    alcGetIntegerv(m_dev, ALC_MINOR_VERSION, 1, &min);
    LOG << "ALC version: " <<  maj << "." << min << "\n";

    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);

    auto initOri = std::array<ALfloat, 6>{0.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    alListenerfv(AL_ORIENTATION, (const ALfloat*) &initOri[0]);

    m_inited = true;
}


OpenALSource* OpenAL::m_addSource()
{
    m_sources.emplace_back( make_unique<OpenALSource>() );
    auto newSource = m_sources.back().get();
    newSource->init();
    return newSource;
}


void OpenAL::list_audio_devices(const ALCchar *devices)
{
    const ALCchar *device = devices, *next = devices + 1;
    size_t len = 0;

    LOG << "OpenAL Devices list:";

    while (device && *device != '\0' && next && *next != '\0')
    {
        LOG << device;
        len = strlen(device);
        device += (len + 1);
        next += (len + 2);
    }
}


int OpenAL::test_error(const char* msg)
{
    ALCenum error;
    error = alGetError();
    if (error != AL_NO_ERROR) {
        LOGE << msg;
        return -1;
    }
    return 1;
}


void OpenAL::destroy()
{
    if (!m_inited) return;
    m_inited = false;

    alcMakeContextCurrent(nullptr);
    if (m_ctx) alcDestroyContext(m_ctx);
    if (m_dev) alcCloseDevice(m_dev);
}

}

#endif