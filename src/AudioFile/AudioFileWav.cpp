//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFileWav.h"

namespace ara::av {

bool AudioFileWav::loadFromMemory(const std::vector<uint8_t>& fileData) {
    m_audioFileFormat = determineAudioFileFormat(fileData);
    if (m_audioFileFormat != AudioFileFormat::Wave) {
        LOG << "AudioFileWav::loadFromMemory Error, file not in wav format";
        return false;
    }
    return decodeFile(fileData);
}

bool AudioFileWav::decodeFile(const std::vector<uint8_t>& fileData) {
    // HEADER CHUNK
    std::string headerChunkID(fileData.begin(), fileData.begin() + 4);
    std::string format(fileData.begin() + 8, fileData.begin() + 12);

    // try and find the start points of key chunks
    int indexOfDataChunk = getIndexOfChunk(fileData, "data", 12);
    int indexOfFormatChunk = getIndexOfChunk(fileData, "fmt ", 12);
    int indexOfXMLChunk = getIndexOfChunk(fileData, "iXML", 12);

    // if we can't find the data or format chunks, or the IDs/formats don't seem to be as expected
    // then it is unlikely we'll able to read this file, so abort
    if (indexOfDataChunk == -1 || indexOfFormatChunk == -1 || headerChunkID != "RIFF" || format != "WAVE") {
        reportError ("ERROR: this doesn't seem to be a valid .WAV file");
        return false;
    }

    // FORMAT CHUNK
    int f = indexOfFormatChunk;
    std::string formatChunkID (fileData.begin() + f, fileData.begin() + f + 4);
    uint16_t audioFormat = twoBytesToInt (fileData, f + 8);
    uint16_t numChannels = twoBytesToInt (fileData, f + 10);
    m_sampleRate = static_cast<uint32_t>(fourBytesToInt (fileData, f + 12));
    uint32_t numBytesPerSecond = fourBytesToInt (fileData, f + 16);
    uint16_t numBytesPerBlock = twoBytesToInt (fileData, f + 20);
    m_bitDepth = static_cast<int>(twoBytesToInt (fileData, f + 22));

    if (m_bitDepth > sizeof (float) * 8) {
        std::string message = "ERROR: you are trying to read a ";
        message += std::to_string (m_bitDepth);
        message += "-bit file using a ";
        message += std::to_string (sizeof (float) * 8);
        message += "-bit sample type";
        reportError (message);
        return false;
    }

    uint16_t numBytesPerSample = static_cast<uint16_t> (m_bitDepth) / 8;

    // check that the audio format is PCM or Float or extensible
    if (audioFormat != WavAudioFormat::PCM
        && audioFormat != WavAudioFormat::IEEEFloat
        && audioFormat != WavAudioFormat::Extensible) {
        reportError ("ERROR: this .WAV file is encoded in a format that this library does not support at present");
        return false;
    }

    // check the number of channels is mono or stereo
    if (numChannels < 1 || numChannels > 128) {
        reportError ("ERROR: this WAV file seems to be an invalid number of channels (or corrupted?)");
        return false;
    }

    // check header data is consistent
    if (numBytesPerSecond != static_cast<uint32_t> ((numChannels * m_sampleRate * m_bitDepth) / 8) || numBytesPerBlock != (numChannels * numBytesPerSample)) {
        reportError ("ERROR: the header data in this WAV file seems to be inconsistent");
        return false;
    }

    // check bit depth is either 8, 16, 24 or 32 bit
    if (m_bitDepth != 8 && m_bitDepth != 16 && m_bitDepth != 24 && m_bitDepth != 32) {
        reportError ("ERROR: this file has a bit depth that is not 8, 16, 24 or 32 bits");
        return false;
    }

    // DATA CHUNK
    int d = indexOfDataChunk;
    std::string dataChunkID (fileData.begin() + d, fileData.begin() + d + 4);
    int32_t dataChunkSize = fourBytesToInt (fileData, d + 4);

    int numSamples = dataChunkSize / (numChannels * m_bitDepth / 8);
    int samplesStartIndex = indexOfDataChunk + 8;

    clearAudioBuffer();
    m_samples_packed.resize(numChannels);

    for (int i = 0; i < numSamples; i++) {
        for (int channel = 0; channel < numChannels; channel++) {
            int sampleIndex = samplesStartIndex + (numBytesPerBlock * i) + channel * numBytesPerSample;
            if ((sampleIndex + (m_bitDepth / 8) - 1) >= fileData.size()) {
                reportError ("ERROR: read file error as the metadata indicates more samples than there are in the file data");
                return false;
            }

            if (m_bitDepth == 8) {
                auto sample = AudioSampleConverter<float>::unsignedByteToSample (fileData[sampleIndex]);
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 16) {
                int16_t sampleAsInt = twoBytesToInt (fileData, sampleIndex);
                auto sample = AudioSampleConverter<float>::sixteenBitIntToSample (sampleAsInt);
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 24) {
                int32_t sampleAsInt = 0;
                sampleAsInt = (fileData[sampleIndex + 2] << 16) | (fileData[sampleIndex + 1] << 8) | fileData[sampleIndex];

                if (sampleAsInt & 0x800000) //  if the 24th bit is set, this is a negative number in 24-bit world
                    sampleAsInt = sampleAsInt | ~0xFFFFFF; // so make sure sign is extended to the 32 bit float

                auto sample = AudioSampleConverter<float>::twentyFourBitIntToSample (sampleAsInt);
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 32) {
                int32_t sampleAsInt = fourBytesToInt (fileData, sampleIndex);
                float sample;

                if (audioFormat == WavAudioFormat::IEEEFloat && std::is_floating_point_v<float>) {
                    memcpy(&sample, &sampleAsInt, sizeof(int32_t));
                } else { // assume PCM
                    sample = AudioSampleConverter<float>::thirtyTwoBitIntToSample (sampleAsInt);
                }

                m_samples_packed[channel].emplace_back(sample);
            } else {
                assert (false);
            }
        }
    }

    // iXML CHUNK
    if (indexOfXMLChunk != -1) {
        int32_t chunkSize = fourBytesToInt (fileData, indexOfXMLChunk + 4);
        iXMLChunk = std::string ((const char*) &fileData[indexOfXMLChunk + 8], chunkSize);
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
    int32_t dataChunkSize = getNumSamplesPerChannel() * (getNumChannels() * m_bitDepth / 8);
    int16_t audioFormat = m_bitDepth == 32 && std::is_floating_point_v<float> ? WavAudioFormat::IEEEFloat : WavAudioFormat::PCM;
    int32_t formatChunkSize = audioFormat == WavAudioFormat::PCM ? 16 : 18;
    auto iXMLChunkSize = static_cast<int32_t> (iXMLChunk.size());

    // HEADER CHUNK
    addStringToFileData (fileData, "RIFF");

    // The file size in bytes is the header chunk size (4, not counting RIFF and WAVE) + the format
    // chunk size (24) + the metadata part of the data chunk plus the actual data chunk size
    int32_t fileSizeInBytes = 4 + formatChunkSize + 8 + 8 + dataChunkSize;
    if (iXMLChunkSize > 0) {
        fileSizeInBytes += (8 + iXMLChunkSize);
    }

    addInt32ToFileData (fileData, fileSizeInBytes);
    addStringToFileData (fileData, "WAVE");

    // FORMAT CHUNK
    addStringToFileData (fileData, "fmt ");
    addInt32ToFileData (fileData, formatChunkSize); // format chunk size (16 for PCM)
    addInt16ToFileData (fileData, audioFormat); // audio format
    addInt16ToFileData (fileData, (int16_t)getNumChannels()); // num channels
    addInt32ToFileData (fileData, (int32_t)m_sampleRate); // sample rate

    auto numBytesPerSecond = static_cast<int32_t>((getNumChannels() * m_sampleRate * m_bitDepth) / 8);
    addInt32ToFileData (fileData, numBytesPerSecond);

    int16_t numBytesPerBlock = getNumChannels() * (m_bitDepth / 8);
    addInt16ToFileData (fileData, numBytesPerBlock);

    addInt16ToFileData (fileData, (int16_t)m_bitDepth);

    if (audioFormat == WavAudioFormat::IEEEFloat) {
        addInt16ToFileData (fileData, 0); // extension size
    }

    // DATA CHUNK
    addStringToFileData (fileData, "data");
    addInt32ToFileData (fileData, dataChunkSize);

    for (int i = 0; i < getNumSamplesPerChannel(); i++) {
        for (int channel = 0; channel < getNumChannels(); channel++) {
            if (m_bitDepth == 8) {
                uint8_t byte = AudioSampleConverter<float>::sampleToUnsignedByte (m_samples_packed[channel][i]);
                fileData.emplace_back(byte);
            } else if (m_bitDepth == 16) {
                int16_t sampleAsInt = AudioSampleConverter<float>::sampleToSixteenBitInt (m_samples_packed[channel][i]);
                addInt16ToFileData (fileData, sampleAsInt);
            } else if (m_bitDepth == 24) {
                int32_t sampleAsIntAgain = AudioSampleConverter<float>::sampleToTwentyFourBitInt (m_samples_packed[channel][i]);

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
                    sampleAsInt = (int32_t) reinterpret_cast<int32_t&> (m_samples_packed[channel][i]);
                } else { // assume PCM
                    sampleAsInt = AudioSampleConverter<float>::sampleToThirtyTwoBitInt (m_samples_packed[channel][i]);
                }
                addInt32ToFileData(fileData, sampleAsInt, Endianness::LittleEndian);
            } else {
                assert (false && "Trying to write a file with unsupported bit depth");
                return false;
            }
        }
    }

    // iXML CHUNK
    if (iXMLChunkSize > 0) {
        addStringToFileData(fileData, "iXML");
        addInt32ToFileData(fileData, iXMLChunkSize);
        addStringToFileData(fileData, iXMLChunk);
    }

    // check that the various sizes we put in the metadata are correct
    if (fileSizeInBytes != static_cast<int32_t> (fileData.size() - 8) || dataChunkSize != (getNumSamplesPerChannel() * getNumChannels() * (m_bitDepth / 8))) {
        reportError ("ERROR: Incorrect file or data chunk size.");
        return false;
    }

    return true;
}

}