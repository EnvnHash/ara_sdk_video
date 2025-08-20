//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFileAiff.h"

namespace ara::av {

AudioFileAiff::AudioFileAiff() {
    m_audioFileFormat = AudioFileFormat::Aiff;
    m_samples.resize(1);
    m_samples[0].resize(0);
}

bool AudioFileAiff::decodeFile(const std::vector<uint8_t>& fileData) {
    auto offset = fourBytesToInt(fileData, m_indexOfDataChunk + 8, Endianness::BigEndian);
    auto m_numBytesPerBlock = m_numBytesPerSample * m_numChannels;
    auto totalNumAudioSampleBytes = m_numSamplesPerChannel * m_numBytesPerBlock;
    auto samplesStartIndex = m_indexOfDataChunk + 16 + offset;

    // sanity check the data
    if ((m_dataChunkSize - 8) != totalNumAudioSampleBytes || totalNumAudioSampleBytes > static_cast<long>(fileData.size() - samplesStartIndex)) {
        reportError ("ERROR: the metadata for this file doesn't seem right");
        return false;
    }

    return parseSamples(fileData, samplesStartIndex, AudioFileFormat::Aiff);
}

bool AudioFileAiff::procFormatChunk(const std::vector<uint8_t>& fileData) {
    int p = m_indexOfDataChunk;
    std::string commChunkID(fileData.begin() + p, fileData.begin() + p + 4);
    m_numChannels = twoBytesToInt(fileData, p + 8, Endianness::BigEndian);
    m_numSamplesPerChannel = fourBytesToInt(fileData, p + 10, Endianness::BigEndian);
    m_bitDepth = static_cast<int32_t>(twoBytesToInt(fileData, p + 14, Endianness::BigEndian));
    m_sampleRate = getAiffSampleRate(fileData, p + 16);

    if (m_bitDepth > sizeof(float) * 8) {
        std::string message = "ERROR: you are trying to read a ";
        message += std::to_string(m_bitDepth);
        message += "-bit file using a ";
        message += std::to_string (sizeof(float) * 8);
        message += "-bit sample type";
        reportError (message);
        return false;
    }

    // check the sample rate was properly decoded
    if (m_sampleRate == 0) {
        reportError ("ERROR: this AIFF file has an unsupported sample rate");
        return false;
    }

    // check the number of channels is mono or stereo
    if (m_numChannels < 1 || m_numChannels > 2) {
        reportError ("ERROR: this AIFF file seems to be neither mono nor stereo (perhaps multi-track, or corrupted?)");
        return false;
    }

    // check bit depth is either 8, 16, 24 or 32-bit
    if (m_bitDepth != 8 && m_bitDepth != 16 && m_bitDepth != 24 && m_bitDepth != 32)  {
        reportError ("ERROR: this file has a bit depth that is not 8, 16, 24 or 32 bits");
        return false;
    }

    return true;
}

bool AudioFileAiff::saveToMemory(std::vector<uint8_t>& fileData, AudioFileFormat format) {
    if (format != AudioFileFormat::Aiff) {
        LOGE << "AudioFileAiff::saveToMemory Error: wrong file format";
        return false;
    }
    return encodeFile(fileData);
}

bool AudioFileAiff::encodeFile(std::vector<uint8_t>& fileData)  {
    int32_t numBytesPerSample = m_bitDepth / 8;
    int32_t numBytesPerFrame = numBytesPerSample * getNumChannels();
    int32_t totalNumAudioSampleBytes = getNumSamplesPerChannel() * numBytesPerFrame;
    int32_t soundDataChunkSize = totalNumAudioSampleBytes + 8;
    auto iXMLChunkSize = static_cast<int32_t>(m_iXMLChunk.size());

    // HEADER CHUNK
    addStringToFileData (fileData, "FORM");

    // The file size in bytes is the header chunk size (4, not counting FORM and AIFF) + the COMM
    // chunk size (26) + the metadata part of the SSND chunk plus the actual data chunk size
    int32_t fileSizeInBytes = 4 + 26 + 16 + totalNumAudioSampleBytes;
    if (iXMLChunkSize > 0) {
        fileSizeInBytes += (8 + iXMLChunkSize);
    }

    addInt32ToFileData (fileData, fileSizeInBytes, Endianness::BigEndian);
    addStringToFileData (fileData, "AIFF");

    // COMM CHUNK
    addStringToFileData(fileData, "COMM");
    addInt32ToFileData(fileData, 18, Endianness::BigEndian); // commChunkSize
    addInt16ToFileData(fileData, getNumChannels(), Endianness::BigEndian); // num channels
    addInt32ToFileData(fileData, getNumSamplesPerChannel(), Endianness::BigEndian); // num samples per channel
    addInt16ToFileData(fileData, m_bitDepth, Endianness::BigEndian); // bit depth
    addSampleRateToAiffData(fileData, m_sampleRate);

    // SSND CHUNK
    addStringToFileData(fileData, "SSND");
    addInt32ToFileData(fileData, soundDataChunkSize, Endianness::BigEndian);
    addInt32ToFileData(fileData, 0, Endianness::BigEndian); // offset
    addInt32ToFileData(fileData, 0, Endianness::BigEndian); // block size

    for (int i = 0; i < getNumSamplesPerChannel(); ++i) {
        for (int channel = 0; channel < getNumChannels(); ++channel) {
            if (m_bitDepth == 8) {
                auto byte = static_cast<uint8_t>(AudioSampleConverter<float>::sampleToSignedByte(m_samples[channel][i]));
                fileData.emplace_back(byte);
            } else if (m_bitDepth == 16) {
                auto sampleAsInt = AudioSampleConverter<float>::sampleToSixteenBitInt(m_samples[channel][i]);
                addInt16ToFileData (fileData, sampleAsInt, Endianness::BigEndian);
            } else if (m_bitDepth == 24) {
                auto sampleAsIntAgain = AudioSampleConverter<float>::sampleToTwentyFourBitInt(m_samples[channel][i]);

                uint8_t bytes[3]{};
                bytes[0] = static_cast<uint8_t>((sampleAsIntAgain >> 16) & 0xFF);
                bytes[1] = static_cast<uint8_t>((sampleAsIntAgain >>  8) & 0xFF);
                bytes[2] = static_cast<uint8_t>(sampleAsIntAgain & 0xFF);

                fileData.emplace_back(bytes[0]);
                fileData.emplace_back(bytes[1]);
                fileData.emplace_back(bytes[2]);
            } else if (m_bitDepth == 32) {
                // write samples as signed integers (no implementation yet for floating point, but looking at WAV implementation should help)
                int32_t sampleAsInt = AudioSampleConverter<float>::sampleToThirtyTwoBitInt(m_samples[channel][i]);
                addInt32ToFileData(fileData, sampleAsInt, Endianness::BigEndian);
            } else {
                assert (false && "Trying to write a file with unsupported bit depth");
                return false;
            }
        }
    }

    // iXML CHUNK
    if (iXMLChunkSize > 0) {
        addStringToFileData(fileData, "iXML");
        addInt32ToFileData(fileData, iXMLChunkSize, Endianness::BigEndian);
        addStringToFileData(fileData, m_iXMLChunk);
    }

    // check that the various sizes we put in the metadata are correct
    if (fileSizeInBytes != static_cast<int32_t>(fileData.size() - 8) || soundDataChunkSize != getNumSamplesPerChannel() *  numBytesPerFrame + 8) {
        reportError ("ERROR: Incorrect file or data chunk size.");
        return false;
    }

    return true;
}

}