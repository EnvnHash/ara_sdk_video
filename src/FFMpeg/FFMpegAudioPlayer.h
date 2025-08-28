/*
 * FFMpegDecode.h
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#pragma once

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO)

#include "FFMpeg/FFMpegDecode.h"
#include "Portaudio/Portaudio.h"

namespace ara::av {

class FFMpegAudioPlayer : public FFMpegDecode {
public:
    void openFile(const ffmpeg::DecodePar& p) override;
    void start(double time) override;
    void stop() override;

    int32_t getAudioWriteBufIdx() { return m_paudio.useCycleBuf() ? static_cast<int32_t>(m_paudio.getCycleBuffer().getWritePos()) : 0; }
    int32_t getAudioReadBufIdx() { return m_paudio.useCycleBuf() ? static_cast<int32_t>(m_paudio.getCycleBuffer().getReadPos()) : 0; }
    auto&   getPaudio() { return m_paudio; }

protected:
    void setCallbackAndConverter(bool useConverter, int32_t sampleRate, AVSampleFormat fmt);
    void procBufSizeFact(audioCbData& data);
    virtual void recvAudioPacket(audioCbData& data);
    void allocateResources(const ffmpeg::DecodePar& p) override;
    void clearResources() override;

    Portaudio               m_paudio;
    size_t                  m_bufSizeFact{};
    size_t                  m_cycBufSize = 160;
    std::vector<float>      m_silenceAudioBuf;
};

}

#endif