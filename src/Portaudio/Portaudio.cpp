#ifdef ARA_USE_PORTAUDIO

#include <algorithm>
#include <map>
#include <cstring>

#include "Portaudio/Portaudio.h"

using namespace std;

namespace ara::av
{

void Portaudio::start()
{
    if (!m_isPlaying)
    {
        // Open an audio I/O stream.
        err = Pa_OpenDefaultStream( &stream,
            0,                  // no input channels
            m_outputParameters.channelCount,    // stereo output
            paFloat32,                          // 32 bit floating point output
            m_sample_rate,
            m_framesPerBuffer,                  // frames per m_buffer
            paCallback,
            (void*)this );

        if ( err != paNoError ) { terminate(); return; }

        err = Pa_StartStream( stream );
        if ( err != paNoError ) { terminate(); return; }

        LOG << " --- Portaudio playing!";

        m_isPlaying = true;
    }
}

void Portaudio::pause()
{
    Pa_StopStream(stream);
    m_isPlaying = false;
}

void Portaudio::stop()
{
    pause();
    Pa_CloseStream(stream);
}

void Portaudio::resume()
{
    Pa_StartStream(stream);
    m_isPlaying = true;
}

bool Portaudio::init()
{
    // Initialize library before making any other calls.
    err = Pa_Initialize();
    if ( err != paNoError ) { terminate(); return false; }

    // check the hardware, get capabilities

    // fill input device with standard parameters
    const PaDeviceInfo* inDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultInputDevice());
    m_inputParameters.device = Pa_GetDefaultInputDevice();
    if (inDevInfo)
        m_inputParameters.channelCount = std::max<int>(2, inDevInfo->maxInputChannels);
    m_inputParameters.sampleFormat = paFloat32;
    if (Pa_GetDefaultInputDevice() > -1)
       m_inputParameters.suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultInputDevice())->defaultLowInputLatency;
    m_inputParameters.hostApiSpecificStreamInfo = nullptr; //See you specific host's API docs for info on using this field

    const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
    m_outputParameters.device = Pa_GetDefaultOutputDevice();
    m_outputParameters.sampleFormat = paFloat32;
    if (outDevInfo)
        m_outputParameters.channelCount = outDevInfo->maxOutputChannels;
    if (Pa_GetDefaultOutputDevice() > -1)
        m_outputParameters.suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency;
    m_outputParameters.hostApiSpecificStreamInfo = nullptr; //See you specific host's API docs for info on using this field

    if (outDevInfo)
        LOG << "Portaudio using default output device: " << outDevInfo->name;

    return true;
}


bool Portaudio::isNrOutChanSupported(int destNrChannels)
{
    if (Pa_GetDefaultOutputDevice() > -1)
    {
        const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
        if (outDevInfo && destNrChannels <= outDevInfo->maxOutputChannels)
            return true;
    }
    return false;
}


bool Portaudio::isSampleRateSupported(double destSampleRate)
{
    // m_outputParameters.channelCount = destNrChannels;
    err = Pa_IsFormatSupported( nullptr, &m_outputParameters, destSampleRate );
    if ( err != paFormatIsSupported )
        return false;
    else
        return true;
}


int Portaudio::getMaxNrOutChannels()
{
    if (Pa_GetDefaultOutputDevice() > -1)
    {
        const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
        if (outDevInfo)
            return outDevInfo->maxOutputChannels;
    }
    return 0;
}


int Portaudio::getValidOutSampleRate(int destSampleRate)
{
    const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == -1) return 0;

    outputParameters.sampleFormat = paFloat32;
    outputParameters.channelCount = outDevInfo->maxOutputChannels;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr; //See you specific host's API docs for info on using this field

    // get a list of supported standard sample rates
    static double standardSampleRates[] = {
            8000.0, 9600.0, 11025.0, 12000.0, 16000.0, 22050.0, 24000.0, 32000.0,
            44100.0, 48000.0, 88200.0, 96000.0, 192000.0, -1 /* negative terminated  list */
    };

    vector<double> supportedSampleRates;
    PaError err;
    for (int i=0; standardSampleRates[i] > 0; i++ )
    {
        err = Pa_IsFormatSupported( nullptr, &outputParameters, standardSampleRates[i] );
        if( err == paFormatIsSupported )
            supportedSampleRates.push_back(standardSampleRates[i]);
    }

    // get the closest sample rate to the desired one
    std::map<double, double> fmtDiff;
    for (auto &it : supportedSampleRates)
        fmtDiff[std::abs(it - destSampleRate)] = it;

    return fmtDiff.size() > 0 ? (int)fmtDiff.begin()->second : 0;
}


/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
** framesPerBuffer refers to on channel
*/
int Portaudio::paCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    // Cast data passed through stream to our structure.
    Portaudio *inst = (Portaudio*)userData;
    float *out = (float*)outputBuffer;

    unique_lock<mutex> l(*inst->getStreamMtx());

    // if cycle m_buffer not filled, set samples to zero and init
    if (inst->m_cycleBuffer.empty())
    {
        memset(out, 0, framesPerBuffer * sizeof(float) * (int)inst->getNrOutChannels());
    } else
    {
        memcpy(out,
                inst->getCycleBuffer()->consume()->getData(),
                sizeof(float) * framesPerBuffer* (int)inst->getNrOutChannels());

        // in case the feed was blocked, unblock it now
        if (inst->m_feedBlock && (*inst->m_feedBlock) && inst->getCycleBuffer()->getFreeSpace() >= inst->m_feedMultiple+1)
            *inst->m_feedBlock = false;
    }

    return 0;
}

}
#endif