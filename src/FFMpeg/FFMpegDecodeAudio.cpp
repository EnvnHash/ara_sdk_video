/*
 * FFMpegDecode.cpp
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO)

#include "FFMpegDecodeAudio.h"

#define STRINGIFY(A) #A

using namespace ara;
using namespace glm;
using namespace std;

namespace ara::av {

int FFMpegDecodeAudio::OpenFile(GLBase* glbase, const std::string& filePath, int destWidth, int destHeight) {
    // open the file init the decoders, contexts, etc..
    if (FFMpegDecode::openFile(glbase, filePath, 4, destWidth, destHeight, true, true)) {
        if (m_audio_nr_channels > 0) {
            // initialize Portaudio
            if (!m_paudio.init({
                .sampleRate = 48000,
                .numChannels = 2
            })) {
                LOGE << "FFMpegDecodeAudio::OpenFile Error could not initialize Portaudio. Aborting";
                return 0;
            }
            m_paudio.printInfo();

            // check if the video's audio m_format is supported by the hardware
            // if the m_format is not supported, or if the sample m_format is not interleaved float values,
            // we need to set up a sample rate m_converter inside FFMpegDecoder
            bool useConverter = false;
            int nrChannels = m_audio_nr_channels;
            if (!m_paudio.isNrOutChanSupported(nrChannels)) {
                useConverter = true;
            }

            int sampleRate = m_audio_codec_ctx->sample_rate;
            if (sampleRate != m_paudio.getSampleRate()) {
                useConverter = true;
                sampleRate = m_paudio.getSampleRate();
            }

            setAudioUpdtCb([this](audioCbData& data) { recvAudioPacket(data); });

            if (useConverter && !FFMpegDecode::setAudioConverter(sampleRate, AV_SAMPLE_FMT_FLT)) {
                LOGE << "FFMpegDecodeAudio::openFile Error could not initialize audio m_converter. Aborting";
                return 0;
            }
            LOG << " --> Setup Sample Conversion success!!";
        }
    }
    return 1;
}

void FFMpegDecodeAudio::start(double time) {
    if (m_audio_nr_channels) {
        m_paudio.start();
    }
    FFMpegDecode::start(time);
}

void FFMpegDecodeAudio::stop() {
    FFMpegDecode::stop();
}

/** Note: nrSamples refers to a single channel */
void FFMpegDecodeAudio::recvAudioPacket(audioCbData& data) {
    if (m_paudio.getCycleBuffer().empty()) {
        // how many portaudio buffers fit in the ffmpeg audio buffer?
        m_bufSizeFact = std::max<size_t>(data.samples / m_paudio.getFramesPerBuffer(), 1);

        // register this multiplication factor to know when to free the feed block again
        m_paudio.getCycleBuffer().allocateBuffers(m_cycBufSize * m_bufSizeFact, m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
    }

    if (static_cast<AVSampleFormat>(data.sampleFmt) != AV_SAMPLE_FMT_FLT) {
        LOGE << "FFmpegDecodeAudio::recvAudioPacket Error. Wrong sample format!";
        return;
    }

    while (m_paudio.getCycleBuffer().getFreeSpace() < m_bufSizeFact) {
        LOGE << "overflow!!!";
        std::this_thread::sleep_for(1ms);
    }

    // copy the new audio frame to Portaudio internal CycleBuffer
    // ffmpeg audio frames are coming in m_format AV_SAMPLE_FMT_FLT, that is float interleaved
    // total size of a ffmpeg frame are nrSamples * nrChannels
    auto buf = reinterpret_cast<float**>(data.buffer);
    for (size_t i=0; i<m_bufSizeFact; i++) {
        m_paudio.getCycleBuffer().feed((float*)&buf[0][0] + i * m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());

    }
}

}

#endif