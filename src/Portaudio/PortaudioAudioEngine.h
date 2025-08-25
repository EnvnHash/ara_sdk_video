//
// Created by sven on 06-08-25.
//

#pragma once

#include <Portaudio/Portaudio.h>
#include <AudioFile/PaAudioFile.h>

namespace ara::av {

class PortaudioAudioEngine : public Portaudio {
public:
    void start() override;
    void playAudioFile(PaAudioFile& samp);
    void stopAudioFile(PaAudioFile& samp);
    void fadeAudioFile(PaAudioFile& samp, fadeType ft, double duration);
    void procSampleQueue();
    PaAudioFile& loadAudioFile(const std::filesystem::path& p);
    PaAudioFile& loadAudioAsset(const std::filesystem::path& p);
    void addToAudioCbQueue(const std::function<void()>& f) { std::unique_lock<std::mutex> l(m_cbQueueMtx); m_audioCbQueue.emplace_back(f); }

private:
    void addAudioFileAtPos(PaAudioFile& af);
    int32_t getActFrameBufPos();

    std::chrono::system_clock::time_point   m_paStartTime{};
    std::list<PaAudioFile*>                 m_samplePlayQueue{};
    std::list<PaAudioFile>                  m_audioFiles{};
    std::mutex                              m_queueMtx;
    std::mutex                              m_cbQueueMtx;
    std::thread                             m_procQueueThread;
    std::list<std::function<void()>>        m_audioCbQueue;
};

}
