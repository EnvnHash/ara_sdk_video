//
// Created by sven on 06-08-25.
//

#include "PaAudioFile.h"
#include "CycleBuffer.h"

using namespace std;

namespace ara::av {

PaAudioFile::PaAudioFile(const AudioFileLoadPar& p) {
    load(p);
    m_volume.setVal(1.f);
}

void PaAudioFile::load(const AudioFileLoadPar& p) {
    try {
        if (filesystem::exists(p.filePath) || p.isAsset) {
            if (p.filePath.extension() == ".wav") {
                m_audioFile = make_unique<AudioFileWav>();
            } else if (p.filePath.extension() == ".aif"){
                m_audioFile = make_unique<AudioFileAiff>();
            } else if (p.filePath.extension() == ".mp3") {
                m_audioFile = make_unique<FFMpegAudioFile>();
            } else {
                throw runtime_error("unsupported file type");
            }
            m_audioFile->load(p);
        } else {
            throw runtime_error("File doesn't exist");
        }
    } catch(const runtime_error& err) {
        LOGE << err.what();
    }
}

float PaAudioFile::consume(int32_t frame, int32_t chan, int32_t sampleRate) {
    m_volume.update();
    if (m_audioFile->getType() == AudioFileFormat::Aiff || m_audioFile->getType() == AudioFileFormat::Wave) {
        return consumeInterpolated(frame, chan, sampleRate);
    } else if (m_audioFile->getType() == AudioFileFormat::FFMpeg) {
        return consumeByBlock(frame, chan, sampleRate);
    }
    return 0.f;
}

float PaAudioFile::consumeByBlock(int32_t frame, int32_t chan, int32_t) {
    auto cyclBuf = m_audioFile->getCycleBuffer();
    if (cyclBuf->empty() || cyclBuf->getFillAmt() == 0) {
        return 0.f;
    } else {
        auto& samples = cyclBuf->getReadBuff();
        return samples[chan * samples.size() / m_audioFile->getNumChannels() + frame + m_audioFile->getReadOffset()];
    }
}

float PaAudioFile::consumeInterpolated(int32_t frame, int32_t chan, int32_t sampleRate) {
    auto playPosInSec = static_cast<double>(frame) / static_cast<double>(sampleRate) + m_playHead;
    if (!m_audioFile->isLoaded() || playPosInSec >= m_audioFile->getLengthInSeconds()) {
        return 0.f;
    }

    auto limitedChan = std::min(chan, m_audioFile->getNumChannels()-1);
    auto offsetInFrames = playPosInSec * static_cast<double>(m_audioFile->getSampleRate());
    float lowerSample = 0.f, upperSample = 0.f;
    if (m_audioFile->getSampleOrder() == SampleOrder::Interleaved) {
        auto& sampBuffer = m_audioFile->getSamplesInterleaved();
        lowerSample = sampBuffer[static_cast<int32_t>(offsetInFrames) * m_audioFile->getNumChannels() + limitedChan];
        upperSample = sampBuffer[static_cast<int32_t>(std::ceil(offsetInFrames)) * m_audioFile->getNumChannels() + limitedChan];
    } else {
        auto& sampBuffer = m_audioFile->getSamplesPacked();
        lowerSample = sampBuffer[limitedChan][static_cast<int32_t>(offsetInFrames)];
        upperSample = sampBuffer[limitedChan][static_cast<int32_t>(std::ceil(offsetInFrames))];
    }
    auto blend = offsetInFrames - std::floor(offsetInFrames);
    return static_cast<float>(lowerSample * (1.0 - blend) + upperSample * blend);
}

void PaAudioFile::advance(int32_t frames, int32_t sampleRate) {
    if (m_audioFile->getType() == AudioFileFormat::Aiff || m_audioFile->getType() == AudioFileFormat::Wave) {
        advancePlayHead(frames, sampleRate);
    } else if (m_audioFile->getType() == AudioFileFormat::FFMpeg) {
        advanceByBlock(frames, sampleRate);
    }
}

void PaAudioFile::advancePlayHead(int32_t frames, int32_t sampleRate) {
    auto blockSizeSec = frames / static_cast<double>(sampleRate);
    m_playHead += blockSizeSec;
    m_reachedEnd = (m_playHead + blockSizeSec) >= m_audioFile->getLengthInSeconds();
    if (m_looping && m_reachedEnd) {
        m_playHead = 0.0;
        m_reachedEnd = false;
        m_playing = false;
    }
}

void PaAudioFile::advanceByBlock(int32_t frames, int32_t) {
    m_audioFile->advance(frames);
}

void PaAudioFile::fade(fadeType tp, double dur, const std::function<void()>& endFunc) {
    if (endFunc) {
        m_volume.setEndFunc(endFunc);
    }
    m_volume.start(tp == fadeType::in ? 0.f : 1.f, tp == fadeType::in ? 1.f : 0.f, dur, false);
}

}