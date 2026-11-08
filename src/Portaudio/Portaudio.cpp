#ifdef ARA_USE_PORTAUDIO

#include "Portaudio/Portaudio.h"

using namespace std;

namespace ara::av {

bool Portaudio::init(const PaInitPar& p) {
    // Initialize a library before making any other calls.
    auto err = Pa_Initialize();
    if (err != paNoError) {
        terminate(err);
        return false;
    }

    m_initPar = p;

    // check the hardware, get capabilities

    // fill the input device with standard parameters
    auto inDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultInputDevice());
    m_inputParameters.device = Pa_GetDefaultInputDevice();

    if (inDevInfo) {
        m_inputParameters.channelCount = !p.numChannels ? std::max<int>(2, inDevInfo->maxInputChannels) : p.numChannels;
    }

    m_inputParameters.sampleFormat = paFloat32;

    if (Pa_GetDefaultInputDevice() > -1) {
        m_inputParameters.suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultInputDevice())->defaultLowInputLatency;
    }

    m_inputParameters.hostApiSpecificStreamInfo = nullptr; //See you specific host's API docs for info on using this field

    const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
    m_outputParameters.device = Pa_GetDefaultOutputDevice();
    m_outputParameters.sampleFormat = paFloat32;
    m_sampleRate = p.sampleRate ? p.sampleRate : outDevInfo->defaultSampleRate;

    if (outDevInfo) {
        m_outputParameters.channelCount = !p.numChannels ? outDevInfo->maxOutputChannels : p.numChannels;
    }

    if (Pa_GetDefaultOutputDevice() > -1) {
        m_outputParameters.suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency;
    }

    m_outputParameters.hostApiSpecificStreamInfo = nullptr; //See you specific host's API docs for info on using this field

    if (outDevInfo) {
        LOG << "Portaudio using default output device: " << outDevInfo->name;
    }

    if (p.framesPerBuffer) {
        m_framesPerBuffer = p.framesPerBuffer;
    }

    m_state = paState::Inited;
    return true;
}

int32_t Portaudio::openStreams() {
    // Open an audio I/O stream.
    return Pa_OpenDefaultStream(&stream,
#ifdef __ANDROID__
                                0,
#else
                                m_inputParameters.channelCount,
#endif
                                m_outputParameters.channelCount,
                                paFloat32,                          // always 32 bit floating point for simplicity
                                static_cast<double>(m_sampleRate),
                                static_cast<unsigned long>(m_framesPerBuffer),
                                paCallback,
                                reinterpret_cast<void*>(this));
}

void Portaudio::start() {
    if (m_state != paState::Running) {
        int err;
        if ((err = openStreams()) != paNoError) {
            terminate(err);
            return;
        }

        m_numChannels = m_outputParameters.channelCount;

        if (m_initPar.allocateBuffers) {
            m_cycleBuffer.allocate(m_initPar.allocateBuffers, m_framesPerBuffer * m_outputParameters.channelCount);
        }

        if ((err = Pa_StartStream(stream)) != paNoError) {
            terminate(err);
            return;
        }

        m_state = paState::Running;
        LOG << " --- Portaudio started!";
    }
}

void Portaudio::pause() {
    Pa_StopStream(stream);
    m_state = paState::Paused;
}

void Portaudio::stop() {
    pause();
    Pa_CloseStream(stream);
    m_state = paState::Stopped;
    LOG << " --- Portaudio stopped!";
}

void Portaudio::resume() {
    Pa_StartStream(stream);
    m_state = paState::Preparing;
}

bool Portaudio::isNrOutChanSupported(int destNrChannels) {
    if (Pa_GetDefaultOutputDevice() > -1) {
        const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
        if (outDevInfo && destNrChannels <= m_numChannels) {
            return true;
        }
    }
    return false;
}

bool Portaudio::isSampleRateSupported(double destSampleRate) {
    auto err = Pa_IsFormatSupported(nullptr, &m_outputParameters, destSampleRate);
    return err == paFormatIsSupported;
}

int Portaudio::getMaxNrOutChannels() {
    if (Pa_GetDefaultOutputDevice() > -1) {
        const PaDeviceInfo* outDevInfo =  Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
        if (outDevInfo) {
            return outDevInfo->maxOutputChannels;
        }
    }
    return 0;
}

int Portaudio::getValidOutSampleRate(int destSampleRate) {
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
    for (int i=0; standardSampleRates[i] > 0; ++i) {
        auto err = Pa_IsFormatSupported( nullptr, &outputParameters, standardSampleRates[i] );
        if (err == paFormatIsSupported ) {
            supportedSampleRates.push_back(standardSampleRates[i]);
        }
    }

    // get the closest sample rate to the desired one
    std::map<double, double> fmtDiff;
    for (auto &it : supportedSampleRates) {
        fmtDiff[std::abs(it - destSampleRate)] = it;
    }

    return !fmtDiff.empty() ? static_cast<int>(fmtDiff.begin()->second) : 0;
}

/* This routine will be called by the PortAudio engine when audio is needed.
* It may be called at interrupt level on some machines so don't do anything that could mess up the system like calling
* malloc() or free().
 * framesPerBuffer refers to one channel.
 * channel data must be passed interleaved (left[0], right[0], left[1], right[1], etc)
*/
int Portaudio::paCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo*,
                          PaStreamCallbackFlags,
                          void *userData) {
    auto ctx = reinterpret_cast<Portaudio*>(userData);
    auto out = reinterpret_cast<float*>(outputBuffer);

    // if cycle m_buffer not filled, set samples to zero and init
    if (ctx->getState() == paState::Running
        && ctx->useCycleBuf()
        && !ctx->m_cycleBuffer.empty()
        && ctx->getCycleBuffer().getFillAmt() != 0) {
        auto& readBuf = ctx->getCycleBuffer().getReadBuff();
        memcpy(out, readBuf.data(), sizeof(float) * framesPerBuffer * ctx->getNrOutChannels());
        ctx->getCycleBuffer().consumeCountUp();
    } else {
        memset(out, 0, framesPerBuffer * sizeof(float) * ctx->getNrOutChannels());
    }

    if (ctx->getStreamProcCb()) {
        ctx->getStreamProcCb()(inputBuffer, outputBuffer, framesPerBuffer);
    }

    return 0;
}

void Portaudio::printInfo() const {
    LOG << "------------------------------------------------------";
    LOG << "Number of channels: " << m_outputParameters.channelCount;
    LOG << "SampleRate: " << m_sampleRate;
    LOG << "Frames per buffer: " << m_framesPerBuffer;
    LOG << "------------------------------------------------------";
}

}

#endif