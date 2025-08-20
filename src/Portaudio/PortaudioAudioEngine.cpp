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

PaAudioFile& PortaudioAudioEngine::loadAudioAsset(const filesystem::path& p) {
    m_audioFiles.emplace_back(AudioFileLoadPar{
        .filePath = p,
        .isAsset = true,
        .portaudio = this
    });
    return m_audioFiles.back();
}

PaAudioFile& PortaudioAudioEngine::loadAudioFile(const filesystem::path& p) {
    m_audioFiles.emplace_back(AudioFileLoadPar{
        .filePath = p,
        .portaudio = this
    });
    return m_audioFiles.back();
}

void PortaudioAudioEngine::play(PaAudioFile& samp) {
    if (m_cycleBuffer.empty()) {
        LOGE << "Portaudio::play Error: cycleBuffer empty, can't add sample";
        return;
    }
    m_samplePlayQueue.emplace_back(&samp);
}

void PortaudioAudioEngine::procSampleQueue() {
    bool countUp = false;
    ranges::fill(m_cycleBuffer.getWriteBuff(), 0.f);
    for (auto af : m_samplePlayQueue) {
        if (af->usingCycleBuf() || (af->getBuffer() && (af->getPlayPos() < af->getBuffer()->size() || af->isLooping()))) {
            addAudioFileAtPos(*af);
            countUp = true;
        }
    }

    if (countUp) {
        m_cycleBuffer.feedCountUp();
    }
}

void PortaudioAudioEngine::addAudioFileAtPos(PaAudioFile& af) {
    auto outBufPtr = m_cycleBuffer.getWriteBuff().begin();
    auto framesToWrite = af.usingCycleBuf() ? m_framesPerBuffer : std::min(m_framesPerBuffer, static_cast<int32_t>(af.getBuffer()->size() - af.getPlayPos()) / af.getNumChannels());

    for (auto frame = 0; frame < framesToWrite; ++frame) {
        for (auto chan=0; chan < m_numChannels; ++chan, ++outBufPtr) {
            *outBufPtr += af.consume(frame, chan, m_sampleRate);
        }
    }

    af.advance(framesToWrite, m_sampleRate);
}

int32_t PortaudioAudioEngine::getActFrameBufPos() {
    auto diff = duration_cast<chrono::nanoseconds>((chrono::system_clock::now() - m_paStartTime)).count();
    auto frameDur = static_cast<int64_t>(1000000000.0 / static_cast<double>(m_sampleRate)) * m_framesPerBuffer;
    return static_cast<int32_t>(diff % frameDur);
}

}