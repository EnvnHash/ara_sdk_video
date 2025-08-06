//
// Created by sven on 06-08-25.
//

#include "Portaudio/PortaudioAudioEngine.h"

using namespace std;

namespace ara::av {

bool PortaudioAudioEngine::init(const PaInitPar& pa) {
    auto r = Portaudio::init(pa);
    m_paStartTime = chrono::system_clock::now();
    m_streamProcCb = [this](const void*, void*, uint64_t) {
       procSampleQueue();
    };
    return r;
}

Sample& PortaudioAudioEngine::loadSample(const std::filesystem::path& p) {
    m_samples.emplace_back(Sample(p));
    return m_samples.back();
}

void PortaudioAudioEngine::play(Sample& samp) {
    if (m_cycleBuffer.empty()) {
        LOGE << "Portaudio::play Error: cycleBuffer empty, can't add sample";
        return;
    }

    //addSampleAtPos(samp, getActFrameBufPos()); // immediately add the first fragment
    m_samplePlayQueue.emplace_back(&samp);
}

void PortaudioAudioEngine::procSampleQueue() {
    bool countUp = false;
    for (auto samp : m_samplePlayQueue) {
        if (samp->getBuffer() && (samp->getPlayPos() < samp->getBuffer()->size() || samp->isLooping())) {
            addSampleAtPos(*samp, 0);
            countUp = true;
        }
    }

    if (countUp) {
        unique_lock<mutex> l(m_streamMtx);
        m_cycleBuffer.feedCountUp();
    }
}

void PortaudioAudioEngine::addSampleAtPos(Sample& samp, int32_t frameBufPos) {
    auto outBufPtr = m_cycleBuffer.getWriteBuff().getData().begin();
    auto framesToWrite = std::min(m_framesPerBuffer, static_cast<int32_t>(samp.getBuffer()->size() - samp.getPlayPos()) / samp.getNumChannels());

    for (auto frame = frameBufPos; frame < framesToWrite; ++frame) {
        for (auto chan=0; chan < m_numChannels; ++chan, ++outBufPtr) {
            *outBufPtr = samp.consume(frame, chan, m_sampleRate);
        }
    }

    samp.advancePlayHead(framesToWrite, m_sampleRate);
}

int32_t PortaudioAudioEngine::getActFrameBufPos() {
    auto diff = duration_cast<chrono::nanoseconds>((chrono::system_clock::now() - m_paStartTime)).count();
    auto frameDur = static_cast<int64_t>(1000000000.0 / static_cast<double>(m_sampleRate)) * m_framesPerBuffer;
    return diff % frameDur;
}

}