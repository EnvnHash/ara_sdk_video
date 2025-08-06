//
// Created by sven on 04-08-25.
//

#include "AudioFile/AudioFile.h"
#include "AudioFile/AudioSampleRateConverter.h"

namespace ara::av {

AudioFile::AudioFile() {
    m_samples.resize(1);
    m_samples[0].resize(0);
}

void AudioFile::printSummary() const {
    LOG << "|======================================|";
    LOG << "Sample Info: ";
    LOG << "Num Channels: " << getNumChannels();
    LOG << "Num Samples Per Channel: " << getNumSamplesPerChannel();
    LOG << "Sample Rate: " << m_sampleRate;
    LOG << "Bit Depth: " << m_bitDepth;
    LOG << "Length in Seconds: " << getLengthInSeconds();
    LOG << "|======================================|";
}

bool AudioFile::setAudioBuffer(const std::deque<std::deque<float>>& newBuffer) {
    int numChannels = static_cast<int>(newBuffer.size());

    if (numChannels <= 0) {
        assert (false && "The buffer you are trying to use has no channels");
    }

    size_t numSamples = newBuffer[0].size();
    m_samples.resize(newBuffer.size());

    for (int k = 0; k < getNumChannels(); k++) {
        assert (newBuffer[k].size() == numSamples);
        m_samples[k].resize(numSamples);
        for (size_t i = 0; i < numSamples; i++) {
            m_samples[k][i] = newBuffer[k][i];
        }
    }

    return true;
}

void AudioFile::setNumSamplesPerChannel(int numSamples) {
    int originalSize = getNumSamplesPerChannel();
    for (int i = 0; i < getNumChannels(); ++i) {
        m_samples[i].resize(numSamples);
        // set any new samples to zero
        if (numSamples > originalSize) {
            std::fill (m_samples[i].begin() + originalSize, m_samples[i].end(), 0.f);
        }
    }
}

void AudioFile::setNumChannels(int numChannels) {
    int originalNumChannels = getNumChannels();
    int originalNumSamplesPerChannel = getNumSamplesPerChannel();
    m_samples.resize(numChannels);

    // make sure any new channels are set to the right size and filled with zeros
    if (numChannels > originalNumChannels) {
        for (int i = originalNumChannels; i < numChannels; i++) {
            m_samples[i].resize(originalNumSamplesPerChannel);
            std::fill (m_samples[i].begin(), m_samples[i].end(), 0.f);
        }
    }
}

bool AudioFile::load(const std::string& filePath, SampleOrder order) {
    std::ifstream file(filePath, std::ios::binary);

    // check the file exists
    if (!file.good()) {
        reportError("ERROR: File doesn't exist or otherwise can't load file\n"  + filePath);
        return false;
    }

    std::vector<uint8_t> fileData;
    file.unsetf(std::ios::skipws);
    file.seekg(0, std::ios::end);
    size_t length = file.tellg();
    file.seekg(0, std::ios::beg);

    // allocate
    fileData.resize(length);
    file.read(reinterpret_cast<char*>(fileData.data()), static_cast<int32_t>(length));
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
        m_sampleOrder = order;
        auto audioFileFormat = determineAudioFileFormat(fileData);
        return loadFromMemory(fileData, audioFileFormat);
    }
}

bool AudioFile::loadFromMemory(const std::vector<uint8_t>& fileData, AudioFileFormat aff) {
    if (!procHeaderChunk(fileData, aff)) {
        return false;
    }

    if (!procFormatChunk(fileData)) {
        return false;
    }

    std::string dataChunkID(fileData.begin() + m_indexOfDataChunk, fileData.begin() + m_indexOfDataChunk + 4);
    m_dataChunkSize = fourBytesToInt(fileData, m_indexOfDataChunk + 4);
    m_numBytesPerSample = m_bitDepth / 8;
    clearAudioBuffer();
    m_samples.resize(m_sampleOrder == SampleOrder::Packed ? m_numChannels : 1);

    auto res = decodeFile(fileData);
    prociXMLChunk(fileData);

    m_loaded = true;
    return res;
}

bool AudioFile::procHeaderChunk(const std::vector<uint8_t>& fileData, AudioFileFormat aff) {
    std::string headerChunkID(fileData.begin(), fileData.begin() + 4);
    std::string format(fileData.begin() + 8, fileData.begin() + 12);

    if (aff == AudioFileFormat::Aiff) {
        m_audioFormat = format == "AIFF" ? AIFFAudioFormat::Uncompressed : format == "AIFC" ? AIFFAudioFormat::Compressed : AIFFAudioFormat::Error;
        m_indexOfDataChunk = getIndexOfChunk(fileData, "COMM", 12, Endianness::BigEndian);
        m_indexOfSoundDataChunk = getIndexOfChunk(fileData, "SSND", 12, Endianness::BigEndian);
    } else {
        m_indexOfDataChunk = getIndexOfChunk(fileData, "data", 12);
        m_indexOfFormatChunk = getIndexOfChunk(fileData, "fmt ", 12);
    }

    m_indexOfXMLChunk = getIndexOfChunk(fileData, "iXML", 12, aff == AudioFileFormat::Aiff ? Endianness::BigEndian : Endianness::LittleEndian);

    if (aff == AudioFileFormat::Wave) {
        if (m_indexOfDataChunk == -1 || m_indexOfFormatChunk == -1 || headerChunkID != "RIFF" || format != "WAVE") {
            reportError ("ERROR: this doesn't seem to be a valid .WAV file");
            return false;
        }
        return true;
    } else {
        if (m_indexOfSoundDataChunk == -1 || m_indexOfDataChunk == -1 || headerChunkID != "FORM" || m_audioFormat == AIFFAudioFormat::Error) {
            reportError ("ERROR: this doesn't seem to be a valid AIFF file");
            return false;
        }
        return true;
    }
}

void AudioFile::prociXMLChunk(const std::vector<uint8_t>& fileData) {
    // iXML CHUNK
    if (m_indexOfXMLChunk != -1) {
        int32_t chunkSize = fourBytesToInt(fileData, m_indexOfXMLChunk + 4);
        m_iXMLChunk = std::string ((const char*) &fileData[m_indexOfXMLChunk + 8], chunkSize);
    }
}

bool AudioFile::parseSamples(const std::vector<uint8_t>& fileData, int32_t samplesStartIndex, AudioFileFormat aff) {
    if (m_bitDepth != 8 && m_bitDepth != 16 && m_bitDepth != 24 && m_bitDepth != 32) {
        LOGE << "AudioFile::parseSamples Error: wrong bit depth";
        return false;
    }

    std::unordered_map<int32_t, std::function<float(const SampleParseData&)>> parseMap {
        { 8, [&](const SampleParseData& sd){ return parse8BitSample(sd); }},
        {16, [&](const SampleParseData& sd){ return parse16BitSample(sd); }},
        {24, [&](const SampleParseData& sd){ return parse24BitSample(sd); }},
        {32, [&](const SampleParseData& sd){ return parse32BitSample(sd); }}
    };

    SampleParseData sd{ .sampleIndex = 0, .channel = 0, .aff = aff, .fileData = fileData };

    for (int i = 0; i < m_numSamplesPerChannel; ++i) {
        for (int channel = 0; channel < m_numChannels; channel++) {
            sd.channel = channel;
            sd.sampleIndex = samplesStartIndex + (m_numBytesPerBlock * i) + channel * m_numBytesPerSample;
            if ((sd.sampleIndex + m_numBytesPerSample - 1) >= fileData.size()) {
                reportError ("ERROR: read file error as the metadata indicates more samples than there are in the file data");
                return false;
            }

            auto sample = parseMap[m_bitDepth](sd);
            if (m_sampleOrder == SampleOrder::Packed) {
                m_samples[sd.channel].emplace_back(sample);
            } else {
                m_samples[0].emplace_back(sample);
            }
        }
    }
    return true;
}

float AudioFile::parse8BitSample(const SampleParseData& sd) {
    return sd.aff == AudioFileFormat::Aiff ?
            AudioSampleConverter<float>::signedByteToSample(static_cast<int8_t>(sd.fileData[sd.sampleIndex]))
            : AudioSampleConverter<float>::unsignedByteToSample(sd.fileData[sd.sampleIndex]);
}

float AudioFile::parse16BitSample(const SampleParseData& sd) {
    auto sampleAsInt = twoBytesToInt(sd.fileData, sd.sampleIndex, sd.aff == AudioFileFormat::Aiff ? Endianness::BigEndian : Endianness::LittleEndian);
    return AudioSampleConverter<float>::sixteenBitIntToSample(sampleAsInt);
}

float AudioFile::parse24BitSample(const SampleParseData& sd) {
    int32_t sampleAsInt = 0;
    if (sd.aff == AudioFileFormat::Aiff) {
        sampleAsInt = (sd.fileData[sd.sampleIndex] << 16) | (sd.fileData[sd.sampleIndex + 1] << 8) | sd.fileData[sd.sampleIndex + 2];
    } else {
        sampleAsInt = (sd.fileData[sd.sampleIndex + 2] << 16) | (sd.fileData[sd.sampleIndex + 1] << 8) | sd.fileData[sd.sampleIndex];
    }
    if (sampleAsInt & 0x800000) { //  if the 24th bit is set, this is a negative number in 24-bit world
        sampleAsInt = sampleAsInt | ~0xFFFFFF; // so make sure sign is extended to the 32 bit float
    }
    return AudioSampleConverter<float>::twentyFourBitIntToSample(sampleAsInt);
}

float AudioFile::parse32BitSample(const SampleParseData& sd) {
    int32_t sampleAsInt = sd.aff == AudioFileFormat::Aiff ? fourBytesToInt(sd.fileData, sd.sampleIndex, Endianness::BigEndian)
                                                          : fourBytesToInt (sd.fileData, sd.sampleIndex);
    float sample;
    if (sd.aff == AudioFileFormat::Wave && m_audioFormat == WavAudioFormat::IEEEFloat) {
        memcpy(&sample, &sampleAsInt, sizeof(int32_t));
    } else if (sd.aff == AudioFileFormat::Aiff && m_audioFormat == AIFFAudioFormat::Compressed) {
        sample = reinterpret_cast<float&>(sampleAsInt);
    } else { // assume PCM
        sample = AudioSampleConverter<float>::thirtyTwoBitIntToSample(sampleAsInt);
    }
    return sample;
}

bool AudioFile::save(const std::string& filePath, AudioFileFormat format) {
    std::vector<uint8_t> fileData;
    return saveToMemory(fileData, format) && writeDataToFile(fileData, filePath);
}

bool AudioFile::writeDataToFile(const std::vector<uint8_t>& fileData, const std::string& filePath) {
    std::ofstream outputFile(filePath, std::ios::binary);
    if (!outputFile.is_open()) {
        return false;
    }
    outputFile.write((const char*)fileData.data(), static_cast<int64_t>(fileData.size()));
    outputFile.close();
    return true;
}

void AudioFile::addStringToFileData(std::vector<uint8_t>& fileData, std::string s) {
    for (size_t i = 0; i < s.length(); ++i) {
        fileData.emplace_back((uint8_t) s[i]);
    }
}

void AudioFile::addInt32ToFileData(std::vector<uint8_t>& fileData, int32_t i, Endianness endianness) {
    uint8_t bytes[4]{};
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

    for (unsigned char & byte : bytes) {
        fileData.emplace_back(byte);
    }
}

void AudioFile::addInt16ToFileData(std::vector<uint8_t>& fileData, int16_t i, Endianness endianness) {
    uint8_t bytes[2]{};
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
    for (auto & i : m_samples) {
        i.clear();
    }
    m_samples.clear();
}

AudioFileFormat AudioFile::determineAudioFileFormat(const std::vector<uint8_t>& fileData) {
    if (fileData.size() < 4) {
        return AudioFileFormat::Error;
    }

    std::string header(fileData.begin(), fileData.begin() + 4);

    if (header == "RIFF") {
        return AudioFileFormat::Wave;
    } else if (header == "FORM") {
        return AudioFileFormat::Aiff;
    } else {
        return AudioFileFormat::Error;
    }
}

int32_t AudioFile::fourBytesToInt(const std::vector<uint8_t>& source, int startIndex, Endianness endianness) {
    if (source.size() >= (startIndex + 4)) {
        if (endianness == Endianness::LittleEndian) {
            return (source[startIndex + 3] << 24) | (source[startIndex + 2] << 16) | (source[startIndex + 1] << 8) | source[startIndex];
        } else {
            return (source[startIndex] << 24) | (source[startIndex + 1] << 16) | (source[startIndex + 2] << 8) | source[startIndex + 3];
        }
    } else {
        assert (false && "Attempted to read four bytes from vector at position where out of bounds access would occur");
    }
}

int16_t AudioFile::twoBytesToInt(const std::vector<uint8_t>& source, int startIndex, Endianness endianness) {
    return (endianness == Endianness::LittleEndian) ?
           (source[startIndex + 1] << 8) | source[startIndex]
                                                    : (source[startIndex] << 8) | source[startIndex + 1];
}

int AudioFile::getIndexOfChunk(const std::vector<uint8_t>& source, const std::string& chunkHeaderID, int startIndex, Endianness endianness) {
    constexpr int dataLen = 4;
    if (chunkHeaderID.size() != dataLen) {
        assert (false && "Invalid chunk header ID string");
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
        }
        i += (dataLen + chunkSize);
    }

    return -1;
}

void AudioFile::reportError(const std::string& errorMessage) const {
    if (m_logErrorsToConsole) {
        LOGE << errorMessage;
    }
}

}