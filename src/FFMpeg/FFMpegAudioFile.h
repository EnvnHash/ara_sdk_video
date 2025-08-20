//
// Created by sven on 19-08-25.
//

#pragma once

#include "AudioFile/AudioFile.h"
#include "FFMpeg/FFMpegAudioPlayer.h"

namespace ara::av {

class FFMpegAudioFile : public AudioFile, FFMpegAudioPlayer {
public:
    FFMpegAudioFile();
    ~FFMpegAudioFile() override = default;

    bool load(const AudioFileLoadPar& p) override;
    void openFile(const ffmpeg::DecodePar& p) override;
    void recvAudioPacket(audioCbData& data) override;
    CycleBuffer<std::vector<float>>* getCycleBuffer() override { return &m_cyclBuf; }
    void advance(int32_t frames) override;
    bool usingCycleBuf() override { return true; }

private:
    bool decodeFile (const std::vector<uint8_t>& fileData) override { return false; }
    bool procFormatChunk(const std::vector<uint8_t>& fileData) override { return false; }
    bool saveToMemory(std::vector<uint8_t> &fileData, AudioFileFormat format) override { return false; }

    Portaudio* m_extPortaudio=nullptr;
    CycleBuffer<std::vector<float>> m_cyclBuf;
};

}