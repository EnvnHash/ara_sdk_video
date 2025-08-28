/*
 * FFMpegAudioPlayer.cpp
 *
 *  Created on: 29.01.2018
 *      Author: sven
 */

#if defined(ARA_USE_FFMPEG) && defined(ARA_USE_PORTAUDIO)

#include <GLBase.h>
#include "FFMpegAudioPlayer.h"

using namespace glm;
using namespace std;
using namespace ara::av::ffmpeg;

namespace ara::av {

void FFMpegAudioPlayer::openFile(const ffmpeg::DecodePar& p) {
    FFMpegDecode::openFile(p);

    try {
        if (m_audioNumChannels > 0) {
            if (!m_paudio.init({
                .sampleRate = 48000,
                .numChannels = 2
            })) {
                throw runtime_error("FFMpegDecodeAudio::OpenFile Error could not initialize Portaudio. Aborting");
            }

            m_paudio.printInfo();

            // check if the video's audio m_format is supported by the hardware
            // if the m_format is not supported, or if the sample m_format is not interleaved float values,
            // we need to set up a sample rate m_converter inside FFMpegDecoder
            bool useConverter = false;
            auto nrChannels = m_audioNumChannels;
            if (!m_paudio.isNrOutChanSupported(nrChannels)) {
                useConverter = true;
            }

            auto sampleRate = m_audioCodecCtx->sample_rate;
            if (sampleRate != m_paudio.getSampleRate()) {
                useConverter = true;
                sampleRate = m_paudio.getSampleRate();
            }

            setCallbackAndConverter(useConverter, sampleRate, AV_SAMPLE_FMT_FLT);
        }
    } catch (std::runtime_error& e) {
        LOGE << "FFMpegDecodeAudio::openFile Error: " << e.what();
    }
}

void FFMpegAudioPlayer::setCallbackAndConverter(bool useConverter, int32_t sampleRate, AVSampleFormat fmt) {
    setAudioUpdtCb([this](audioCbData& data) { recvAudioPacket(data); });

    if (useConverter && !FFMpegDecode::setAudioConverter(sampleRate, fmt)) {
        throw runtime_error("FFMpegDecodeAudio::openFile Error could not initialize audio m_converter. Aborting");
    }

    m_silenceAudioBuf.resize(m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
    std::ranges::fill(m_silenceAudioBuf, 0.f);
}

void FFMpegAudioPlayer::start(double time) {
    if (m_audioNumChannels && !m_paudio.isRunning()) {
        m_paudio.start();
    }
    FFMpegDecode::start(time);
}

void FFMpegAudioPlayer::stop() {
    FFMpegDecode::stop();
}

void FFMpegAudioPlayer::procBufSizeFact(audioCbData& data) {
    if (!m_bufSizeFact) {
        // how many portaudio buffers fit in the ffmpeg audio buffer?
        auto f = static_cast<float>(data.samples) / static_cast<float>(m_paudio.getFramesPerBuffer());
        if (std::fmod(f, 1.0) != 0.0) {
            LOGE << "FFMpegAudioPlayer::recvAudioPacket error, packet sample size not a multiple of audio frameBuffer size!";
        }
        m_bufSizeFact = std::max<size_t>(static_cast<int32_t>(f), 1);
    }
}

/** Note: nrSamples refers to a single channel */
void FFMpegAudioPlayer::recvAudioPacket(audioCbData& data) {
    procBufSizeFact(data);

    if (m_paudio.getCycleBuffer().empty()) {
        // register this multiplication factor to know when to free the feed block again
        m_paudio.getCycleBuffer().allocate(m_cycBufSize * m_bufSizeFact,
                                           m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
    }

    if (static_cast<AVSampleFormat>(data.sampleFmt) != AV_SAMPLE_FMT_FLT) {
        LOGE << "FFMpegDecodeAudio::recvAudioPacket Error. Wrong sample format!";
        return;
    }

    // in case there is a positive difference between the audio and video stream duration,
    // and we just came across the loop point, and the difference in duration as silence into the cycle buffer
    if (m_audioToVideoDurationDiff > 0 && m_firstFramePresented
        && m_lastPtss[toType(streamType::audio)] > data.ptss) {
        auto addNumSilenceFrames = static_cast<int32_t>((m_audioToVideoDurationDiff * m_paudio.getSampleRate()) / m_paudio.getFramesPerBuffer());
        for (size_t i=0; i<addNumSilenceFrames; i++) {
            m_paudio.getCycleBuffer().feed(m_silenceAudioBuf.data());
        }
    }

    // copy the new audio frame to Portaudio internal CycleBuffer
    // ffmpeg audio frames are coming in m_format AV_SAMPLE_FMT_FLT, that is float interleaved
    // total size of a ffmpeg frame are nrSamples * nrChannels
    auto buf = reinterpret_cast<float**>(data.buffer);
    for (size_t i=0; i<m_bufSizeFact; i++) {
        while (m_paudio.getCycleBuffer().isFilled()) {
            std::this_thread::sleep_for(100us);
        }
        m_paudio.getCycleBuffer().feed((float*)&buf[0][0] + i * m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
    }

    m_lastPtss[toType(streamType::audio)] = data.ptss;
}

void FFMpegAudioPlayer::allocateResources(const DecodePar& p) {
    FFMpegDecode::allocateResources(p);
}

void FFMpegAudioPlayer::clearResources() {
    FFMpegDecode::clearResources();
}

}

#endif