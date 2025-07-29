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
    void					start(double time);
    void 					stop();
    void                    recv_audio_packet(audioCbData& data);

private:
    Portaudio               m_paudio;
    uint32_t               	m_cycBufSize=256; // queue size in nr of PortAudio Frames, must be more or less equal to video queue in length
    size_t               	m_bufSizeFact{};
};
}

#endif