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

int FFMpegDecodeAudio::OpenFile(GLBase* glbase, const std::string& _filePath, int destWidth, int destHeight) {

    // open the file init the decoders, contexts, etc..
    if (FFMpegDecode::OpenFile(glbase, _filePath, 4, destWidth, destHeight, true, true))
    {
        if (m_audio_nr_channels > 0)
        {
            // initialize Portaudio
            if (!m_paudio.init())
            {
                LOGE << "FFMpegDecodeAudio::OpenFile Error could not initialize Portaudio. Aborting";
                return 0;
            }

            // check if the video's audio m_format is supported by the hardware
            // if the m_format is not supported, or if the sample m_format is not interleaved float values,
            // we need to setup a sample rate m_converter inside FFMpegDecoder
            bool useConverter = false;
            int nrChannels = m_audio_nr_channels;
            if (!m_paudio.isNrOutChanSupported(nrChannels))
            {
                useConverter = true;
                nrChannels = m_paudio.getMaxNrOutChannels();
            }

            int sampleRate = m_audio_codec_ctx->sample_rate;
            if (!m_paudio.isSampleRateSupported((double) sampleRate))
            {
                useConverter = true;
                sampleRate = m_paudio.getValidOutSampleRate(sampleRate);
            }

            sampleRate = m_paudio.getValidOutSampleRate(sampleRate);

            if (useConverter && !FFMpegDecode::setAudioConverter(sampleRate, AV_SAMPLE_FMT_FLT))
            {
                LOGE << "FFMpegDecodeAudio::OpenFile Error could not initialize audio m_converter. Aborting";
                return 0;
            }

            FFMpegDecode::setAudioConverter(sampleRate, AV_SAMPLE_FMT_FLT);

            m_paudio.setSampleRate(sampleRate);
            m_paudio.setNrOutputChannel(nrChannels);
            m_paudio.setFeedBlock(&m_audioQueueFull);

            setAudioUpdtCb([this](audioCbData& data) { recv_audio_packet(data); });

            LOG << " --> Setup Sample Conversion success!!";
        }
    }

    return 1;
}


void FFMpegDecodeAudio::start(double time)
{
    if (m_audio_nr_channels) m_paudio.start();
    FFMpegDecode::start(time);
}


void FFMpegDecodeAudio::stop()
{
    // if (m_audio_nr_channels)  m_paudio.stop();
    FFMpegDecode::stop();
}


/** Note: nrSamples refers to a single channel */
void FFMpegDecodeAudio::recv_audio_packet(audioCbData& data) {
    unique_lock<mutex> l(m_paudio.getStreamMtx());

    if (m_paudio.getCycleBuffer().empty()) {
        // the individual buffers should have the size of the hardware output m_buffer size,
        // in order to just memcpy them inside the Portaudio callback
        // anyway we need to make sure, that the hardware output is not bigger, than ffmpegs
        // audio decoder
        if ((uint32_t)m_paudio.getFramesPerBuffer() > data.samples) {
            if (m_paudio.isRunning()) {
                m_paudio.pause();
            }
            m_paudio.setFramesPerBuffer(data.samples);
        }

        // how many portaudio buffers fit in the ffmpeg audio buffer?
        m_bufSizeFact = std::max<size_t>(data.samples / m_paudio.getFramesPerBuffer(), 1);

        // register this multiplication factor to know when to free the feed block again
        m_paudio.setFeedBlockMultiple(m_bufSizeFact);

        // be sure the cycle buffers in independent of the m_bufSizeFact multiples
        m_paudio.getCycleBuffer().allocateBuffers(m_cycBufSize * m_bufSizeFact, m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
    }

    if (!m_paudio.getCycleBuffer().empty()) {
        if (static_cast<AVSampleFormat>(data.sampleFmt) != AV_SAMPLE_FMT_FLT) {
            LOGE << "FFmpegDecodeAudio::recv_audio_packet Error. Wrong sample format!";
            return;
        }
        
        // copy the new audio frame to Portaudio internal CycleBuffer
        // ffmpeg audio frames are coming in m_format AV_SAMPLE_FMT_FLT, that is float interleaved
        // total size of a ffmpeg frame are nrSamples * nrChannels
        auto buf = reinterpret_cast<float**>(data.buffer);
        for (size_t i=0; i<m_bufSizeFact; i++) {
            m_paudio.getCycleBuffer().feed((float*)&buf[0][0] + i * m_paudio.getFramesPerBuffer() * m_paudio.getNrOutChannels());
        }

        // check if the audio queue is full, if this is the case set the block flag
        if (m_paudio.getCycleBuffer().getFreeSpace() < m_bufSizeFact)
            m_audioQueueFull = true;

        if (!m_paudio.isRunning()) {
            m_paudio.start();
        }
    }
}

}

#endif