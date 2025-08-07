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

class FFMpegDecodeAudio : public FFMpegDecode {
public:
    int                     OpenFile(GLBase* glbase, const std::string& filePath, int destWidth, int destHeight);
    void					start(double time) override;
    void 					stop() override;
    void                    recvAudioPacket(audioCbData& data);
    int32_t                 getAudioWriteBufIdx() { return m_paudio.useCycleBuf() ? m_paudio.getCycleBuffer().getWritePos() : 0; }
    int32_t                 getAudioReadBufIdx() { return m_paudio.useCycleBuf() ? m_paudio.getCycleBuffer().getReadPos() : 0; }

private:
    Portaudio               m_paudio;
    uint32_t               	m_cycBufSize = 128; // queue size in nr of PortAudio Frames, must be more or less equal to video queue in length
    size_t               	m_bufSizeFact{};
};
}

#endif