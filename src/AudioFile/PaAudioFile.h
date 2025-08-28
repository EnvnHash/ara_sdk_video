//
// Created by sven on 06-08-25.
//

#pragma once

#include "AudioFile/AudioFileAiff.h"
#include "AudioFile/AudioFileWav.h"
#include "FFMpeg/FFMpegAudioFile.h"
#include <AnimVal.h>

namespace ara::av {

enum class fadeType : int32_t { in=0, out };

class PaAudioFile  {
public:
    PaAudioFile() = default;
    explicit PaAudioFile(const AudioFileLoadPar& p);
    void load(const AudioFileLoadPar& p);
    void printInfo() const { if (m_audioFile) m_audioFile->printSummary(); }

    float consume(int32_t frame, int32_t chan, int32_t sampleRate);
    float consumeByBlock(int32_t frame, int32_t chan, int32_t sampleRate);
    float consumeInterpolated(int32_t frame, int32_t chan, int32_t sampleRate);

    void advance(int32_t frames, int32_t sampleRate);
    void advancePlayHead(int32_t frame, int32_t sampleRate);
    void advanceByBlock(int32_t frames, int32_t sampleRate);

    void fade(fadeType tp, double time, const std::function<void()>& endFunc);

    [[nodiscard]] bool isLooping() const    { return m_looping; }
    float getVolume()                       { return m_volume.getVal(); }
    int64_t& getPlayPos()                   { return m_posPointer; }
    auto getBuffer()   { return m_audioFile ? &m_audioFile->getSamplesInterleaved() : nullptr; }
    int32_t getNumChannels()                { return m_audioFile ? m_audioFile->getNumChannels() : 0; }
    int32_t getSampleRate()                 { return m_audioFile ? static_cast<int32_t>(m_audioFile->getSampleRate()) : 0; }
    auto getType()                   { return m_audioFile ? m_audioFile->getType() : AudioFileFormat{}; }
    bool usingCycleBuf()                    { return m_audioFile && m_audioFile->usingCycleBuf(); }
    bool isPlaying() const                  { return m_playing; }
    bool reachedEnd() const                 { return m_reachedEnd; }
    void reset()                            { m_reachedEnd = false; m_playing = false; m_playHead = 0.0; m_posPointer = 0; }

    void setLooping(bool val)       { m_looping = val; }
    void setPlayPos(int64_t pos)    { m_posPointer = pos; }
    void setPlaying(bool val)       { m_playing = val; }

protected:
    std::unique_ptr<AudioFile>  m_audioFile;
    bool                        m_looping = false;
    bool                        m_playing = false;
    bool                        m_reachedEnd = false;
    int64_t                     m_posPointer = 0;
    double                      m_playHead = 0;
    AnimVal<float>              m_volume;
};

}