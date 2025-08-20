//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFileWav.h"

namespace ara::av {

AudioFileWav::AudioFileWav() {
    m_audioFileFormat = AudioFileFormat::Wave;
    m_samples.resize(1);
    m_samples[0].resize(0);
}

bool AudioFileWav::decodeFile(const std::vector<uint8_t>& fileData) {
    m_numSamplesPerChannel = m_dataChunkSize / (m_numChannels * m_numBytesPerSample);
    int samplesStartIndex = m_indexOfDataChunk + 8;
    return parseSamples(fileData, samplesStartIndex, AudioFileFormat::Wave);
}

bool AudioFileWav::procFormatChunk(const std::vector<uint8_t>& fileData) {
    int f = m_indexOfFormatChunk;
    std::string formatChunkID(fileData.begin() + f, fileData.begin() + f + 4);
    m_audioFormat = twoBytesToInt(fileData, f + 8);
    m_numChannels = twoBytesToInt(fileData, f + 10);
    m_sampleRate = static_cast<uint32_t>(fourBytesToInt(fileData, f + 12));
    uint32_t numBytesPerSecond = fourBytesToInt(fileData, f + 16);
    m_numBytesPerBlock = twoBytesToInt(fileData, f + 20);
    m_bitDepth = static_cast<int>(twoBytesToInt(fileData, f + 22));

    if (m_bitDepth > sizeof (float) * 8) {
        std::string message = "ERROR: you are trying to read a ";
        message += std::to_string(m_bitDepth);
        message += "-bit file using a ";
        message += std::to_string (sizeof (float) * 8);
        message += "-bit sample type";
        reportError (message);
        return false;
    }

    auto numBytesPerSample = m_bitDepth / 8;

    // check that the audio format is PCM or Float or extensible
    if (m_audioFormat != WavAudioFormat::PCM
        && m_audioFormat != WavAudioFormat::IEEEFloat
        && m_audioFormat != WavAudioFormat::Extensible) {
        reportError ("ERROR: this .WAV file is encoded in a format that this library does not support at present");
        return false;
    }

    // check the number of channels is mono or stereo
    if (m_numChannels < 1 || m_numChannels > 128) {
        reportError ("ERROR: this WAV file seems to be an invalid number of channels (or corrupted?)");
        return false;
    }

    // check header data is consistent
    if (numBytesPerSecond != static_cast<uint32_t>((m_numChannels * m_sampleRate * m_bitDepth) / 8) || m_numBytesPerBlock != (m_numChannels * numBytesPerSample)) {
        reportError ("ERROR: the header data in this WAV file seems to be inconsistent");
        return false;
    }

    // check bit depth is either 8, 16, 24 or 32 bit
    if (m_bitDepth != 8 && m_bitDepth != 16 && m_bitDepth != 24 && m_bitDepth != 32) {
        reportError ("ERROR: this file has a bit depth that is not 8, 16, 24 or 32 bits");
        return false;
    }

    return true;
}

bool AudioFileWav::saveToMemory(std::vector<uint8_t>& fileData, AudioFileFormat format) {
    if (format != AudioFileFormat::Wave) {
        LOGE << "AudioFileWav::saveToMemory Error: wrong file format";
        return false;
    }
    return encodeFile(fileData);
}

bool AudioFileWav::encodeFile(std::vector<uint8_t>& fileData) {
    auto dataChunkSize = getNumSamplesPerChannel() * (getNumChannels() * m_bitDepth / 8);
    auto audioFormat = m_bitDepth == 32 && std::is_floating_point_v<float> ? WavAudioFormat::IEEEFloat : WavAudioFormat::PCM;
    auto formatChunkSize = audioFormat == WavAudioFormat::PCM ? 16 : 18;
    auto iXMLChunkSize = static_cast<int32_t>(m_iXMLChunk.size());

    // HEADER CHUNK
    addStringToFileData (fileData, "RIFF");

    // The file size in bytes is the header chunk size (4, not counting RIFF and WAVE) + the format
    // chunk size (24) + the metadata part of the data chunk plus the actual data chunk size
    auto fileSizeInBytes = 4 + formatChunkSize + 8 + 8 + dataChunkSize;
    if (iXMLChunkSize > 0) {
        fileSizeInBytes += (8 + iXMLChunkSize);
    }

    addInt32ToFileData(fileData, fileSizeInBytes);
    addStringToFileData(fileData, "WAVE");

    // FORMAT CHUNK
    addStringToFileData(fileData, "fmt ");
    addInt32ToFileData(fileData, formatChunkSize); // format chunk size (16 for PCM)
    addInt16ToFileData(fileData, audioFormat); // audio format
    addInt16ToFileData(fileData, (int16_t)getNumChannels()); // num channels
    addInt32ToFileData(fileData, (int32_t)m_sampleRate); // sample rate

    auto numBytesPerSecond = static_cast<int32_t>((getNumChannels() * m_sampleRate * m_bitDepth) / 8);
    addInt32ToFileData (fileData, numBytesPerSecond);

    m_numBytesPerBlock = getNumChannels() * (m_bitDepth / 8);
    addInt16ToFileData(fileData, m_numBytesPerBlock);
    addInt16ToFileData(fileData, static_cast<int16_t>(m_bitDepth));

    if (audioFormat == WavAudioFormat::IEEEFloat) {
        addInt16ToFileData(fileData, 0); // extension size
    }

    // DATA CHUNK
    addStringToFileData(fileData, "data");
    addInt32ToFileData(fileData, dataChunkSize);

    for (int i = 0; i < getNumSamplesPerChannel(); i++) {
        for (int channel = 0; channel < getNumChannels(); channel++) {
            if (m_bitDepth == 8) {
                uint8_t byte = AudioSampleConverter<float>::sampleToUnsignedByte(m_samples[channel][i]);
                fileData.emplace_back(byte);
            } else if (m_bitDepth == 16) {
                int16_t sampleAsInt = AudioSampleConverter<float>::sampleToSixteenBitInt(m_samples[channel][i]);
                addInt16ToFileData (fileData, sampleAsInt);
            } else if (m_bitDepth == 24) {
                int32_t sampleAsIntAgain = AudioSampleConverter<float>::sampleToTwentyFourBitInt(m_samples[channel][i]);

                uint8_t bytes[3];
                bytes[2] = static_cast<uint8_t>((sampleAsIntAgain >> 16) & 0xFF);
                bytes[1] = static_cast<uint8_t>((sampleAsIntAgain >>  8) & 0xFF);
                bytes[0] = static_cast<uint8_t>(sampleAsIntAgain & 0xFF);

                fileData.emplace_back(bytes[0]);
                fileData.emplace_back(bytes[1]);
                fileData.emplace_back(bytes[2]);
            } else if (m_bitDepth == 32) {
                int32_t sampleAsInt;
                if (audioFormat == WavAudioFormat::IEEEFloat) {
                    sampleAsInt = reinterpret_cast<int32_t&>(m_samples[channel][i]);
                } else { // assume PCM
                    sampleAsInt = AudioSampleConverter<float>::sampleToThirtyTwoBitInt(m_samples[channel][i]);
                }
                addInt32ToFileData(fileData, sampleAsInt, Endianness::LittleEndian);
            } else {
                assert(false && "Trying to write a file with unsupported bit depth");
            }
        }
    }

    // iXML CHUNK
    if (iXMLChunkSize > 0) {
        addStringToFileData(fileData, "iXML");
        addInt32ToFileData(fileData, iXMLChunkSize);
        addStringToFileData(fileData, m_iXMLChunk);
    }

    // check that the various sizes we put in the metadata are correct
    if (fileSizeInBytes != static_cast<int32_t> (fileData.size() - 8) || dataChunkSize != (getNumSamplesPerChannel() * getNumChannels() * (m_bitDepth / 8))) {
        reportError ("ERROR: Incorrect file or data chunk size.");
        return false;
    }

    return true;
}

}