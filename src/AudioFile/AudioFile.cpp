//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFile.h"
#include "AudioFile/AudioSampleRateConverter.h"

namespace ara::av {

AudioFile::AudioFile() {
    m_samples_packed.resize(1);
    m_samples_packed[0].resize(0);
}

void AudioFile::printSummary() const {
    LOG << "|======================================|";
    LOG << "Num Channels: " << getNumChannels();
    LOG << "Num Samples Per Channel: " << getNumSamplesPerChannel();
    LOG << "Sample Rate: " << m_sampleRate;
    LOG << "Bit Depth: " << m_bitDepth;
    LOG << "Length in Seconds: " << getLengthInSeconds();
    LOG << "|======================================|";
}

bool AudioFile::setAudioBuffer(const std::deque<std::deque<float>>& newBuffer) {
    int numChannels = (int)newBuffer.size();

    if (numChannels <= 0) {
        assert (false && "The buffer you are trying to use has no channels");
        return false;
    }

    size_t numSamples = newBuffer[0].size();

    // set the number of channels
    m_samples_packed.resize (newBuffer.size());

    for (int k = 0; k < getNumChannels(); k++) {
        assert (newBuffer[k].size() == numSamples);
        m_samples_packed[k].resize (numSamples);
        for (size_t i = 0; i < numSamples; i++) {
            m_samples_packed[k][i] = newBuffer[k][i];
        }
    }

    return true;
}

void AudioFile::setNumSamplesPerChannel(int numSamples) {
    int originalSize = getNumSamplesPerChannel();
    for (int i = 0; i < getNumChannels(); ++i) {
        m_samples_packed[i].resize (numSamples);
        // set any new samples to zero
        if (numSamples > originalSize) {
            std::fill (m_samples_packed[i].begin() + originalSize, m_samples_packed[i].end(), 0.f);
        }
    }
}

void AudioFile::setNumChannels(int numChannels) {
    int originalNumChannels = getNumChannels();
    int originalNumSamplesPerChannel = getNumSamplesPerChannel();
    m_samples_packed.resize (numChannels);

    // make sure any new channels are set to the right size and filled with zeros
    if (numChannels > originalNumChannels) {
        for (int i = originalNumChannels; i < numChannels; i++) {
            m_samples_packed[i].resize (originalNumSamplesPerChannel);
            std::fill (m_samples_packed[i].begin(), m_samples_packed[i].end(), 0.f);
        }
    }
}

bool AudioFile::load(const std::string& filePath) {
    std::ifstream file (filePath, std::ios::binary);

    // check the file exists
    if (!file.good()) {
        reportError ("ERROR: File doesn't exist or otherwise can't load file\n"  + filePath);
        return false;
    }

    std::vector<uint8_t> fileData;
    file.unsetf (std::ios::skipws);
    file.seekg (0, std::ios::end);
    size_t length = file.tellg();
    file.seekg (0, std::ios::beg);

    // allocate
    fileData.resize(length);
    file.read(reinterpret_cast<char*> (fileData.data()), length);
    file.close();

    if (file.gcount() != length) {
        reportError ("ERROR: Couldn't read entire file\n" + filePath);
        return false;
    }

    // Handle very small files that will break our attempt to read the first header info from them
    if (fileData.size() < 12) {
        reportError ("ERROR: File is not a valid audio file\n" + filePath);
        return false;
    } else {
        return loadFromMemory(fileData);
    }
}

bool AudioFile::save(const std::string& filePath, AudioFileFormat format) {
    std::vector<uint8_t> fileData;
    return saveToMemory (fileData, format) && writeDataToFile (fileData, filePath);
}

bool AudioFile::writeDataToFile (const std::vector<uint8_t>& fileData, std::string filePath) {
    std::ofstream outputFile (filePath, std::ios::binary);
    if (!outputFile.is_open()) {
        return false;
    }
    outputFile.write ((const char*)fileData.data(), fileData.size());
    outputFile.close();
    return true;
}

void AudioFile::addStringToFileData (std::vector<uint8_t>& fileData, std::string s) {
    for (size_t i = 0; i < s.length(); ++i) {
        fileData.emplace_back((uint8_t) s[i]);
    }
}

void AudioFile::addInt32ToFileData (std::vector<uint8_t>& fileData, int32_t i, Endianness endianness) {
    uint8_t bytes[4];

    if (endianness == Endianness::LittleEndian) {
        bytes[3] = (i >> 24) & 0xFF;
        bytes[2] = (i >> 16) & 0xFF;
        bytes[1] = (i >> 8) & 0xFF;
        bytes[0] = i & 0xFF;
    } else {
        bytes[0] = (i >> 24) & 0xFF;
        bytes[1] = (i >> 16) & 0xFF;
        bytes[2] = (i >> 8) & 0xFF;
        bytes[3] = i & 0xFF;
    }

    for (int j = 0; j < 4; j++) {
        fileData.emplace_back(bytes[j]);
    }
}

void AudioFile::addInt16ToFileData (std::vector<uint8_t>& fileData, int16_t i, Endianness endianness) {
    uint8_t bytes[2];

    if (endianness == Endianness::LittleEndian) {
        bytes[1] = (i >> 8) & 0xFF;
        bytes[0] = i & 0xFF;
    } else {
        bytes[0] = (i >> 8) & 0xFF;
        bytes[1] = i & 0xFF;
    }

    fileData.emplace_back(bytes[0]);
    fileData.emplace_back(bytes[1]);
}

void AudioFile::clearAudioBuffer() {
    for (size_t i = 0; i < m_samples_packed.size();i++) {
        m_samples_packed[i].clear();
    }
    m_samples_packed.clear();
}

AudioFileFormat AudioFile::determineAudioFileFormat (const std::vector<uint8_t>& fileData) {
    if (fileData.size() < 4) {
        return AudioFileFormat::Error;
    }

    std::string header (fileData.begin(), fileData.begin() + 4);

    if (header == "RIFF") {
        return AudioFileFormat::Wave;
    } else if (header == "FORM") {
        return AudioFileFormat::Aiff;
    } else {
        return AudioFileFormat::Error;
    }
}

int32_t AudioFile::fourBytesToInt (const std::vector<uint8_t>& source, int startIndex, Endianness endianness) {
    if (source.size() >= (startIndex + 4)) {
        if (endianness == Endianness::LittleEndian) {
            return (source[startIndex + 3] << 24) | (source[startIndex + 2] << 16) | (source[startIndex + 1] << 8) | source[startIndex];
        } else {
            return (source[startIndex] << 24) | (source[startIndex + 1] << 16) | (source[startIndex + 2] << 8) | source[startIndex + 3];
        }
    } else {
        assert (false && "Attempted to read four bytes from vector at position where out of bounds access would occur");
        return 0; // this is a dummy value as we don't have one to return
    }
}

int16_t AudioFile::twoBytesToInt (const std::vector<uint8_t>& source, int startIndex, Endianness endianness) {
    return (endianness == Endianness::LittleEndian) ?
           (source[startIndex + 1] << 8) | source[startIndex]
                                                    : (source[startIndex] << 8) | source[startIndex + 1];
}

int AudioFile::getIndexOfChunk (const std::vector<uint8_t>& source, const std::string& chunkHeaderID, int startIndex, Endianness endianness) {
    constexpr int dataLen = 4;
    if (chunkHeaderID.size() != dataLen) {
        assert (false && "Invalid chunk header ID string");
        return -1;
    }

    int i = startIndex;
    while (i < source.size() - dataLen) {
        if (memcmp (&source[i], chunkHeaderID.data(), dataLen) == 0) {
            return i;
        }

        i += dataLen;

        // If somehow we don't have 4 bytes left to read, then exit with -1
        if ((i + 4) >= source.size()) {
            return -1;
        }

        int32_t chunkSize = fourBytesToInt (source, i, endianness);
        // Assume chunk size is invalid if it's greater than the number of bytes remaining in source
        if (chunkSize > (source.size() - i - dataLen) || (chunkSize < 0)) {
            assert (false && "Invalid chunk size");
            return -1;
        }
        i += (dataLen + chunkSize);
    }

    return -1;
}

void AudioFile::reportError (const std::string& errorMessage) {
    if (m_logErrorsToConsole) {
        LOGE << errorMessage;
    }
}

}