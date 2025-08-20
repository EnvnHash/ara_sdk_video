//
// Created by sven on 19-08-25.
//

#include "FFMpeg/FFMpegAudioFile.h"

using namespace std;

namespace ara::av {

FFMpegAudioFile::FFMpegAudioFile() {
    m_audioFileFormat = AudioFileFormat::FFMpeg;
}

bool FFMpegAudioFile::load(const AudioFileLoadPar& p) {
    m_extPortaudio = p.portaudio;
    openFile({ .filePath = p.filePath });
    m_numChannels = m_audioCodecCtx->ch_layout.nb_channels;
    FFMpegDecode::start(0.0);
    return true;
}

void FFMpegAudioFile::openFile(const ffmpeg::DecodePar& p) {
    FFMpegDecode::openFile(p);
    try {
        if (m_audioNumChannels > 0 && m_extPortaudio) {
            setCallbackAndConverter(m_audioCodecCtx->sample_fmt != AV_SAMPLE_FMT_FLTP,
                                    static_cast<int32_t>(m_sampleRate), AV_SAMPLE_FMT_FLTP);
        }
    } catch (std::runtime_error& e) {
        LOGE << "FFMpegDecodeAudio::openFile Error: " << e.what();
    }
}

void FFMpegAudioFile::recvAudioPacket(audioCbData& data) {
    if (static_cast<AVSampleFormat>(data.sampleFmt) != AV_SAMPLE_FMT_FLTP || !m_extPortaudio) {
        LOGE << "FFMpegDecodeAudio::recvAudioPacket Error. Wrong sample format!";
        return;
    }

    if (m_cyclBuf.empty()) {
        m_cyclBuf.allocate(2, data.samples * data.nChannels);
    }

    while (m_cyclBuf.isFilled()) {
        std::this_thread::sleep_for(200us);
    }

    auto buf = reinterpret_cast<float**>(data.buffer);
    for (auto i = 0; i<data.nChannels; ++i) {
        std::copy(buf[i], buf[i] + data.samples, std::next(m_cyclBuf.getWriteBuff().begin(), i * data.samples));
    }

    m_cyclBuf.feedCountUp();
}

void FFMpegAudioFile::advance(int32_t frames) {
    if (m_cyclBuf.empty()) {
        return;
    }

    m_readOffset += frames;
    if (m_readOffset >= m_cyclBuf.getBuff(0)->size() / m_numChannels) {
        m_readOffset = 0;
        m_cyclBuf.consumeCountUp();
    }
}

}