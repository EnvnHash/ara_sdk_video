//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFileAiff.h"

namespace ara::av {

bool AudioFileAiff::loadFromMemory(const std::vector<uint8_t>& fileData) {
    m_audioFileFormat = determineAudioFileFormat(fileData);
    if (m_audioFileFormat != AudioFileFormat::Aiff) {
        LOG << "AudioFileWav::loadFromMemory Error, file not in aiff format";
        return false;
    }
    return decodeFile(fileData);
}

bool AudioFileAiff::decodeFile(const std::vector<uint8_t>& fileData) {
    // HEADER CHUNK
    std::string headerChunkID (fileData.begin(), fileData.begin() + 4);
    //int32_t fileSizeInBytes = fourBytesToInt (fileData, 4, Endianness::BigEndian) + 8;
    std::string format (fileData.begin() + 8, fileData.begin() + 12);

    int audioFormat = format == "AIFF" ? AIFFAudioFormat::Uncompressed : format == "AIFC" ? AIFFAudioFormat::Compressed : AIFFAudioFormat::Error;

    // try and find the start points of key chunks
    int indexOfCommChunk = getIndexOfChunk (fileData, "COMM", 12, Endianness::BigEndian);
    int indexOfSoundDataChunk = getIndexOfChunk (fileData, "SSND", 12, Endianness::BigEndian);
    int indexOfXMLChunk = getIndexOfChunk (fileData, "iXML", 12, Endianness::BigEndian);

    // if we can't find the data or format chunks, or the IDs/formats don't seem to be as expected
    // then it is unlikely we'll able to read this file, so abort
    if (indexOfSoundDataChunk == -1 || indexOfCommChunk == -1 || headerChunkID != "FORM" || audioFormat == AIFFAudioFormat::Error) {
        reportError ("ERROR: this doesn't seem to be a valid AIFF file");
        return false;
    }

    // COMM CHUNK
    int p = indexOfCommChunk;
    std::string commChunkID (fileData.begin() + p, fileData.begin() + p + 4);
    int16_t numChannels = twoBytesToInt (fileData, p + 8, Endianness::BigEndian);
    int32_t numSamplesPerChannel = fourBytesToInt (fileData, p + 10, Endianness::BigEndian);
    m_bitDepth = (int) twoBytesToInt (fileData, p + 14, Endianness::BigEndian);
    m_sampleRate = getAiffSampleRate (fileData, p + 16);

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
    if (numChannels < 1 ||numChannels > 2) {
        reportError ("ERROR: this AIFF file seems to be neither mono nor stereo (perhaps multi-track, or corrupted?)");
        return false;
    }

    // check bit depth is either 8, 16, 24 or 32-bit
    if (m_bitDepth != 8 && m_bitDepth != 16 && m_bitDepth != 24 && m_bitDepth != 32)  {
        reportError ("ERROR: this file has a bit depth that is not 8, 16, 24 or 32 bits");
        return false;
    }

    // SSND CHUNK
    int s = indexOfSoundDataChunk;
    std::string soundDataChunkID (fileData.begin() + s, fileData.begin() + s + 4);
    int32_t soundDataChunkSize = fourBytesToInt (fileData, s + 4, Endianness::BigEndian);
    int32_t offset = fourBytesToInt (fileData, s + 8, Endianness::BigEndian);
    //int32_t blockSize = fourBytesToInt (fileData, s + 12, Endianness::BigEndian);

    int numBytesPerSample = m_bitDepth / 8;
    int numBytesPerFrame = numBytesPerSample * numChannels;
    int totalNumAudioSampleBytes = numSamplesPerChannel * numBytesPerFrame;
    int samplesStartIndex = s + 16 + (int)offset;

    // sanity check the data
    if ((soundDataChunkSize - 8) != totalNumAudioSampleBytes || totalNumAudioSampleBytes > static_cast<long>(fileData.size() - samplesStartIndex)) {
        reportError ("ERROR: the metadatafor this file doesn't seem right");
        return false;
    }

    clearAudioBuffer();
    m_samples_packed.resize (numChannels);

    for (int i = 0; i < numSamplesPerChannel; ++i) {
        for (int channel = 0; channel < numChannels; channel++) {
            int sampleIndex = samplesStartIndex + (numBytesPerFrame * i) + channel * numBytesPerSample;

            if ((sampleIndex + (m_bitDepth / 8) - 1) >= fileData.size()) {
                reportError ("ERROR: read file error as the metadata indicates more samples than there are in the file data");
                return false;
            }

            if (m_bitDepth == 8) {
                auto sample = AudioSampleConverter<float>::signedByteToSample (static_cast<int8_t> (fileData[sampleIndex]));
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 16) {
                int16_t sampleAsInt = twoBytesToInt (fileData, sampleIndex, Endianness::BigEndian);
                auto sample = AudioSampleConverter<float>::sixteenBitIntToSample (sampleAsInt);
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 24) {
                int32_t sampleAsInt = 0;
                sampleAsInt = (fileData[sampleIndex] << 16) | (fileData[sampleIndex + 1] << 8) | fileData[sampleIndex + 2];

                if (sampleAsInt & 0x800000) //  if the 24th bit is set, this is a negative number in 24-bit world
                    sampleAsInt = sampleAsInt | ~0xFFFFFF; // so make sure sign is extended to the 32 bit float

                auto sample = AudioSampleConverter<float>::twentyFourBitIntToSample (sampleAsInt);
                m_samples_packed[channel].emplace_back(sample);
            } else if (m_bitDepth == 32) {
                int32_t sampleAsInt = fourBytesToInt (fileData, sampleIndex, Endianness::BigEndian);
                float sample;

                if (audioFormat == AIFFAudioFormat::Compressed) {
                    sample = reinterpret_cast<float&>(sampleAsInt);
                } else { // assume PCM
                    sample = AudioSampleConverter<float>::thirtyTwoBitIntToSample(sampleAsInt);
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
    int32_t iXMLChunkSize = static_cast<int32_t> (iXMLChunk.size());

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
    addStringToFileData (fileData, "COMM");
    addInt32ToFileData (fileData, 18, Endianness::BigEndian); // commChunkSize
    addInt16ToFileData (fileData, getNumChannels(), Endianness::BigEndian); // num channels
    addInt32ToFileData (fileData, getNumSamplesPerChannel(), Endianness::BigEndian); // num samples per channel
    addInt16ToFileData (fileData, m_bitDepth, Endianness::BigEndian); // bit depth
    addSampleRateToAiffData (fileData, m_sampleRate);

    // SSND CHUNK
    addStringToFileData (fileData, "SSND");
    addInt32ToFileData (fileData, soundDataChunkSize, Endianness::BigEndian);
    addInt32ToFileData (fileData, 0, Endianness::BigEndian); // offset
    addInt32ToFileData (fileData, 0, Endianness::BigEndian); // block size

    for (int i = 0; i < getNumSamplesPerChannel(); ++i) {
        for (int channel = 0; channel < getNumChannels(); ++channel) {
            if (m_bitDepth == 8) {
                uint8_t byte = static_cast<uint8_t> (AudioSampleConverter<float>::sampleToSignedByte (m_samples_packed[channel][i]));
                fileData.emplace_back(byte);
            } else if (m_bitDepth == 16) {
                int16_t sampleAsInt = AudioSampleConverter<float>::sampleToSixteenBitInt (m_samples_packed[channel][i]);
                addInt16ToFileData (fileData, sampleAsInt, Endianness::BigEndian);
            } else if (m_bitDepth == 24) {
                int32_t sampleAsIntAgain = AudioSampleConverter<float>::sampleToTwentyFourBitInt (m_samples_packed[channel][i]);

                uint8_t bytes[3];
                bytes[0] = (uint8_t) (sampleAsIntAgain >> 16) & 0xFF;
                bytes[1] = (uint8_t) (sampleAsIntAgain >>  8) & 0xFF;
                bytes[2] = (uint8_t) sampleAsIntAgain & 0xFF;

                fileData.emplace_back(bytes[0]);
                fileData.emplace_back(bytes[1]);
                fileData.emplace_back(bytes[2]);
            } else if (m_bitDepth == 32) {
                // write samples as signed integers (no implementation yet for floating point, but looking at WAV implementation should help)
                int32_t sampleAsInt = AudioSampleConverter<float>::sampleToThirtyTwoBitInt (m_samples_packed[channel][i]);
                addInt32ToFileData (fileData, sampleAsInt, Endianness::BigEndian);
            } else {
                assert (false && "Trying to write a file with unsupported bit depth");
                return false;
            }
        }
    }

    // iXML CHUNK
    if (iXMLChunkSize > 0) {
        addStringToFileData (fileData, "iXML");
        addInt32ToFileData (fileData, iXMLChunkSize, Endianness::BigEndian);
        addStringToFileData (fileData, iXMLChunk);
    }

    // check that the various sizes we put in the metadata are correct
    if (fileSizeInBytes != static_cast<int32_t> (fileData.size() - 8) || soundDataChunkSize != getNumSamplesPerChannel() *  numBytesPerFrame + 8) {
        reportError ("ERROR: Incorrect file or data chunk size.");
        return false;
    }

    return true;
}

}