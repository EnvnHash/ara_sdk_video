//
// Created by sven on 06-08-25.
//

#include "Sample.h"

using namespace std;

namespace ara::av {

Sample::Sample(const std::filesystem::path& p) {
    load(p);
}

void Sample::load(const std::filesystem::path& p) {
    try {
        if (filesystem::exists(p)) {
            if (p.extension() == ".wav") {
                m_audioFile = make_unique<AudioFileWav>();
            } else if (p.extension() == ".aif"){
                m_audioFile = make_unique<AudioFileAiff>();
            } else {
                throw("unsupported file type");
            }
            m_audioFile->load(p.string(), SampleOrder::Interleaved);
        } else {
            throw std::runtime_error("File doesn't exist");
        }
    } catch(const std::runtime_error& err) {
        LOGE << err.what();
    }
}

float Sample::consume(int32_t frame, int32_t chan, int32_t sampleRate) {
    auto playPosInSec = static_cast<double>(frame) / static_cast<double>(sampleRate) + m_playHead;
    if (playPosInSec >= m_audioFile->getLengthInSeconds()) {
        return 0.f;
    }

    auto limitedChan = std::min(chan, m_audioFile->getNumChannels()-1);
    auto offsetInFrames = playPosInSec * static_cast<double>(m_audioFile->getSampleRate());
    auto& sampBuffer = m_audioFile->getSamplesInterleaved();
    auto lowerSample = sampBuffer[static_cast<int32_t>(offsetInFrames) * m_audioFile->getNumChannels() + limitedChan];
    auto upperSample = sampBuffer[static_cast<int32_t>(std::ceil(offsetInFrames)) * m_audioFile->getNumChannels() + limitedChan];
    auto blend = offsetInFrames - std::floor(offsetInFrames);
    return static_cast<float>(lowerSample * (1.0 - blend) + upperSample * blend);
}

void Sample::advancePlayHead(int32_t frames, int32_t sampleRate) {
    m_playHead += frames / static_cast<double>(sampleRate);
    if (m_looping && m_playHead >= m_audioFile->getLengthInSeconds()) {
        m_playHead = 0.0;
    }
}

}