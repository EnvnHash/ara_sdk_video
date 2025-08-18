//
// Created by sven on 06-08-25.
//

#pragma once

#include <Portaudio/Portaudio.h>
#include <AudioFile/Sample.h>

namespace ara::av {

class PortaudioAudioEngine : public Portaudio {
public:
    bool init(const PaInitPar& = PaInitPar()) override;
    void play(Sample& samp);
    void procSampleQueue();
    Sample& loadSample(const std::filesystem::path& p);

private:
    void addSampleAtPos(Sample& samp);
    int32_t getActFrameBufPos();

    std::chrono::system_clock::time_point   m_paStartTime{};
    std::list<Sample*>                      m_samplePlayQueue{};
    std::list<Sample>                       m_samples{};
};

}
