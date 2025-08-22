//
// Created by sven on 06-08-25.
//

#pragma once

#include <Portaudio/Portaudio.h>
#include <AudioFile/PaAudioFile.h>

namespace ara::av {

class PortaudioAudioEngine : public Portaudio {
public:
    bool init(const PaInitPar&) override;
    void start() override;
    void play(PaAudioFile& samp);
    void stopAudioFile(PaAudioFile& samp);
    void procSampleQueue();
    PaAudioFile& loadAudioFile(const std::filesystem::path& p);
    PaAudioFile& loadAudioAsset(const std::filesystem::path& p);
private:
    void addAudioFileAtPos(PaAudioFile& af);
    int32_t getActFrameBufPos();

    std::chrono::system_clock::time_point   m_paStartTime{};
    std::list<PaAudioFile*>                 m_samplePlayQueue{};
    std::list<PaAudioFile>                  m_audioFiles{};
    std::mutex                              m_queueMtx;
    std::thread                             m_procQueueThread;
};

}
