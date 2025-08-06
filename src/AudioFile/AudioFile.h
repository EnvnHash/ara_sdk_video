/** @file AudioFile.h
 *  @author Adam Stark
 *  @copyright Copyright (C) 2017  Adam Stark
 *
 * This file is part of the 'AudioFile' library
 *
 * MIT License
 *
 * Copyright (c) 2017 Adam Stark
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#if defined (_MSC_VER)
#undef max
#undef min
#define NOMINMAX
#endif

// disable some warnings on Windows
#if defined (_MSC_VER)
__pragma(warning (push))
    __pragma(warning (disable : 4244))
    __pragma(warning (disable : 4457))
    __pragma(warning (disable : 4458))
    __pragma(warning (disable : 4389))
    __pragma(warning (disable : 4996))
#elif defined (__GNUC__)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wconversion\"")
_Pragma("GCC diagnostic ignored \"-Wsign-compare\"")
_Pragma("GCC diagnostic ignored \"-Wshadow\"")
#endif

#include "AVCommon.h"
#include "AudioFile/AudioSampleRateConverter.h"

namespace ara::av {

/** The different types of audio file, plus some other types to indicate a failure to load a file, or that one hasn't
* been loaded yet */
enum class AudioFileFormat : int32_t { Error = 0, NotLoaded, Wave, Aiff };
enum WavAudioFormat { PCM = 0x0001, IEEEFloat = 0x0003, ALaw = 0x0006, MULaw = 0x0007, Extensible = 0xFFFE };
enum AIFFAudioFormat { Uncompressed = 0, Compressed, Error };
enum class Endianness : int32_t { LittleEndian = 0, BigEndian };
enum class SampleOrder : int32_t { Packed = 0, Interleaved };
struct SampleParseData {
    int32_t sampleIndex=0;
    int32_t channel=0;
    AudioFileFormat aff{};
    const std::vector<uint8_t>& fileData;
};

class AudioFile {
public:
    AudioFile();
    virtual ~AudioFile() = default;

    /** Constructor, using a given file path to load a file */
    explicit AudioFile(const std::string &filePath, SampleOrder order=SampleOrder::Packed) { load (filePath, order); }

    /** Loads an audio file from a given file path.
     * @Returns true if the file was successfully loaded */
    bool load(const std::string &filePath, SampleOrder order=SampleOrder::Packed);

    /** Loads an audio file from data in memory */
    bool loadFromMemory(const std::vector<uint8_t> &fileData, AudioFileFormat aff);

    virtual bool decodeFile (const std::vector<uint8_t>& fileData) = 0;
    virtual bool procFormatChunk(const std::vector<uint8_t>& fileData) = 0;
    bool procHeaderChunk(const std::vector<uint8_t>& fileData, AudioFileFormat aff);
    void prociXMLChunk(const std::vector<uint8_t>& fileData);
    bool parseSamples(const std::vector<uint8_t>& fileData, int32_t samplesStartIndex, AudioFileFormat aff);

    /** Saves an audio file to a given file path.
     * @Returns true if the file was successfully saved */
    bool save(const std::string &filePath, AudioFileFormat format = AudioFileFormat::Wave);

    /** Saves an audio file to data in memory */
    virtual bool saveToMemory(std::vector<uint8_t> &fileData, AudioFileFormat format) = 0;

    /** @Returns the sample rate */
    [[nodiscard]] uint32_t getSampleRate() const { return m_sampleRate; }

    /** @Returns the number of audio channels in the buffer */
    [[nodiscard]] auto getNumChannels() const { return m_numChannels; }

    /** @Returns the bit depth of each sample */
    [[nodiscard]] auto getBitDepth() const { return m_bitDepth; }

    /** @Returns the number of samples per channel */
    [[nodiscard]] auto getNumSamplesPerChannel() const { return !m_samples.empty() ? static_cast<int32_t>(m_samples[0].size() / (m_sampleOrder == SampleOrder::Packed ? 1 : m_numChannels)) :  0; }

    /** @Returns the length in seconds of the audio file based on the number of samples and sample rate */
    [[nodiscard]] auto getLengthInSeconds() const { return static_cast<float>(getNumSamplesPerChannel()) / static_cast<float>(m_sampleRate); }

    float getSample(int32_t channel, int32_t sampleIdx) { return m_sampleOrder == SampleOrder::Packed ? m_samples[channel][sampleIdx] : m_samples[0][sampleIdx * m_numChannels + channel]; }

    const std::deque<std::deque<float>>& getSamplesPacked() { return m_samples; }
    const std::deque<float>& getSamplesInterleaved() { return m_samples[0]; }

    /** Prints a summary of the audio file to the console */
    void printSummary() const;

    /** Set the audio buffer for this AudioFile by copying samples from another buffer.
     * @Returns true if the buffer was copied successfully. */
    [[maybe_unused]] bool setAudioBuffer(const std::deque<std::deque<float>> &newBuffer);

    /** Sets the audio buffer to a given number of channels and number of samples per channel. This will try to preserve
     * the existing audio, adding zeros to any new channels or new samples in a given channel. */
    void setAudioBufferSize(const int numChannels, const int numSamples)  {
        m_samples.resize(numChannels);
        setNumSamplesPerChannel(numSamples);
    }

    /** Sets the number of samples per channel in the audio buffer. This will try to preserve
     * the existing audio, adding zeros to new samples in a given channel if the number of samples is increased. */
    void setNumSamplesPerChannel(int numSamples);

    /** Sets the number of channels. New channels will have the correct number of samples and be initialised to zero */
    void setNumChannels(int numChannels);

    /** Sets the bit depth for the audio file. If you use the save() function, this bit depth rate will be used */
    void setBitDepth(const int numBitsPerSample) { m_bitDepth = numBitsPerSample; }

    /** Sets the sample rate for the audio file. If you use the save() function, this sample rate will be used */
    void setSampleRate(const uint32_t newSampleRate) { m_sampleRate = newSampleRate; }

    /** Sets whether the library should log error messages to the console. By default this is true */
    void shouldLogErrorsToConsole(bool logErrors) { m_logErrorsToConsole = logErrors; }

    bool isLoaded() { return m_loaded; }

protected:
  //  virtual bool encodeFile(std::vector<uint8_t> &fileData) = 0;

    void clearAudioBuffer();

    float parse8BitSample(const SampleParseData& sd);
    float parse16BitSample(const SampleParseData& sd);
    float parse24BitSample(const SampleParseData& sd);
    float parse32BitSample(const SampleParseData& sd);

    static AudioFileFormat determineAudioFileFormat(const std::vector<uint8_t> &fileData);

    static int32_t fourBytesToInt(const std::vector<uint8_t> &source, int startIndex,
                                         Endianness endianness = Endianness::LittleEndian);
    static int16_t twoBytesToInt(const std::vector<uint8_t> &source, int startIndex,
                                        Endianness endianness = Endianness::LittleEndian);
    static int getIndexOfChunk(const std::vector<uint8_t> &source, const std::string &chunkHeaderID, int startIndex,
                    Endianness endianness = Endianness::LittleEndian);

    static void addStringToFileData(std::vector<uint8_t> &fileData, std::string s);
    static void addInt32ToFileData(std::vector<uint8_t> &fileData, int32_t i, Endianness endianness = Endianness::LittleEndian);
    static void addInt16ToFileData(std::vector<uint8_t> &fileData, int16_t i, Endianness endianness = Endianness::LittleEndian);

    static bool writeDataToFile(const std::vector<uint8_t> &fileData, const std::string& filePath);

    void reportError(const std::string &errorMessage) const;

    std::deque<std::deque<float>> m_samples;
    uint32_t            m_sampleRate = 44100;
    int32_t             m_numChannels = 0;
    uint16_t            m_numBytesPerBlock = 0;
    uint16_t            m_audioFormat = 0;
    SampleOrder         m_sampleOrder = SampleOrder::Packed;
    int                 m_bitDepth = 16;
    bool                m_logErrorsToConsole{true};
    bool                m_loaded{false};

    int32_t     m_numBytesPerSample = 0;
    int32_t     m_numSamplesPerChannel = 0;
    int32_t     m_dataChunkSize = 0;
    int32_t     m_indexOfFormatChunk = 0;
    int32_t     m_indexOfSoundDataChunk = 0;
    int32_t     m_indexOfDataChunk = 0;
    int32_t     m_indexOfXMLChunk = 0;
    std::string m_iXMLChunk;
};

}

#if defined (_MSC_VER)
    __pragma(warning (pop))
#elif defined (__GNUC__)
    _Pragma("GCC diagnostic pop")
#endif
