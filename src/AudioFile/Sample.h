//
// Created by sven on 06-08-25.
//

#pragma once

#include "AudioFile/AudioFileAiff.h"
#include "AudioFile/AudioFileWav.h"

namespace ara::av {

class Sample  {
public:
    Sample() = default;
    Sample(const std::filesystem::path& p);
    void load(const std::filesystem::path& p);

    void printInfo() const { if (m_audioFile) m_audioFile->printSummary(); }
    float consume(int32_t frame, int32_t chan, int32_t sampleRate);
    void advancePlayHead(int32_t frame, int32_t sampleRate);

    bool isLooping()                        { return m_looping; }
    int64_t& getPlayPos()                   { return m_posPointer; }
    const std::deque<float>* getBuffer()    { return m_audioFile ? &m_audioFile->getSamplesInterleaved() : nullptr; }
    int32_t getNumChannels()                { return m_audioFile ? m_audioFile->getNumChannels() : 0; }

    void setLooping(bool val)       { m_looping = val; }
    void setPlayPos(int64_t pos)    { m_posPointer = pos; }

protected:
    std::unique_ptr<AudioFile>  m_audioFile;
    bool                        m_looping = false;
    int64_t                     m_posPointer = 0;
    double                      m_playHead = 0;
};

}