//
// Created by sven on 06-08-25.
//

#include "Portaudio/PortaudioAudioEngine.h"
#include "AnimVal.h"

using namespace std;

namespace ara::av {

void PortaudioAudioEngine::start() {
    Portaudio::start();
    m_state = paState::Preparing;
    m_paStartTime = chrono::system_clock::now();
    startProcQueue();
}

void PortaudioAudioEngine::startProcQueue() {
    m_procQueueThread = std::thread([this] {
        while (toType(m_state) >= toType(paState::Preparing)) {
            {
                unique_lock l(m_cbQueueMtx);
                for (auto&it : m_audioCbQueue) {
                    it();
                }
                m_audioCbQueue.clear();
            }
            procSampleQueue();
        }
        m_queueLoopExit.notify();
    });
    m_procQueueThread.detach();
}

void PortaudioAudioEngine::pause() {
    m_state = paState::Paused;
    m_cycleBuffer.forceSkipFillWait();
    m_queueLoopExit.wait();
    Portaudio::pause();
    int filled = m_cycleBuffer.getFillAmt();
    for (int i=0; i<filled; i++) {
        m_cycleBuffer.consumeCountUp();
    }
}

void PortaudioAudioEngine::resume() {
    Portaudio::resume();
    m_state = paState::Preparing;
    startProcQueue();
}

void PortaudioAudioEngine::stop() {
    m_state = paState::Stopped;
    m_cycleBuffer.forceSkipFillWait();
    m_queueLoopExit.wait();
    Portaudio::stop();
    for (auto &it : m_audioFiles) {
        stopAudioFile(it);
    }
    m_cycleBuffer.clear();
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

void PortaudioAudioEngine::playAudioFile(PaAudioFile& samp) {
    if (m_cycleBuffer.empty()) {
        LOGE << "Portaudio::play Error: cycleBuffer empty, can't add sample";
        return;
    }
    samp.reset();
    samp.setPlaying(true);
    unique_lock<mutex> l(m_queueMtx);
    m_samplePlayQueue.emplace_back(&samp);
}

void PortaudioAudioEngine::stopAudioFile(PaAudioFile& samp) {
    samp.setPlaying(false);
    unique_lock<mutex> l(m_queueMtx);
    std::erase_if(m_samplePlayQueue, [&](auto it) { return it == &samp; });
}

void PortaudioAudioEngine::fadeAudioFile(PaAudioFile& samp, fadeType ft, double duration) {
    samp.fade(ft, duration, [ft, this, &samp]{
        if (ft == fadeType::out) {
            addToAudioCbQueue([this, &samp] { stopAudioFile(samp); });
        }
    });
}

void PortaudioAudioEngine::procSampleQueue() {
    if (m_cycleBuffer.empty()) {
        return;
    }

    m_cycleBuffer.waitUntilNotFilled();
    bool countUp = false;

    {
        unique_lock l(m_queueMtx);
        erase_if(m_samplePlayQueue, [](auto af) { return af->reachedEnd(); });
        ranges::fill(m_cycleBuffer.getWriteBuff(), 0.f);

        for (auto af : m_samplePlayQueue) {
            if (af->usingCycleBuf()
                || (af->getBuffer()
                    && (af->getPlayPos() < af->getBuffer()->size() || af->isLooping()))) {
                addAudioFileAtPos(*af);
                countUp = true;
            }
        }
    }

    if (countUp) {
        m_cycleBuffer.feedCountUp();
        if (m_state == paState::Preparing && m_cycleBuffer.isFilled()) {
            m_state = paState::Running;
        }
    }
}

void PortaudioAudioEngine::addAudioFileAtPos(PaAudioFile& af) {
    auto outBufPtr = m_cycleBuffer.getWriteBuff().begin();
    auto framesToWrite = af.usingCycleBuf() ? m_framesPerBuffer : std::min(m_framesPerBuffer, static_cast<int32_t>(af.getBuffer()->size() - af.getPlayPos()) / af.getNumChannels());

    for (auto frame = 0; frame < framesToWrite; ++frame) {
        for (auto chan=0; chan < m_numChannels; ++chan, ++outBufPtr) {
            *outBufPtr += af.consume(frame, chan, m_sampleRate) * af.getVolume();
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