//
// Created by sven on 04-08-25.
//

#pragma once

#include "AudioFile/AudioFile.h"

namespace ara::av {

struct AiffUtilities {
    /** Decode an 80-bit (10 byte) sample rate to a double */
    static inline double decodeAiffSampleRate(const uint8_t *bytes) {
        // Note: Sample rate is 80 bits made up of
        // * 1 sign bit
        // * 15 exponent bits
        // * 64 mantissa bits

        // Sign
        // Extract the sign (most significant bit of byte 0)
        int sign = (bytes[0] & 0x80) ? -1 : 1;

        // Exponent
        // byte 0: ignore the sign and shift the most significant bits to the left by one byte
        auto msbShifted = (static_cast<uint16_t> (bytes[0] & 0x7F) << 8);

        // calculate exponent by combining byte 0 and byte 1 and subtract bias
        auto exponent = (msbShifted | static_cast<uint16_t> (bytes[1])) - 16383;

        // Mantissa
        // Extract the mantissa (remaining 64 bits) by looping over the remaining
        // bytes and combining them while shifting the result to the left by
        // 8 bits each time
        uint64_t mantissa = 0;
        for (int i = 2; i < 10; ++i) {
            mantissa = (mantissa << 8) | bytes[i];
        }

        // Normalize the mantissa (implicit leading 1 for normalized values)
        double normalisedMantissa = static_cast<double> (mantissa) / (1ULL << 63);

        // Combine sign, exponent, and mantissa into a double
        return sign * std::ldexp (normalisedMantissa, exponent);
    }

    /** Encode a double as an 80-bit (10-byte) sample rate */
    static inline void encodeAiffSampleRate(double sampleRate, uint8_t *bytes)  {
        // Determine the sign
        int sign = (sampleRate < 0) ? -1 : 1;
        if (sign == -1) {
            sampleRate = -sampleRate;
        }

        // Set most significant bit of byte 0 for the sign
        bytes[0] = (sign == -1) ? 0x80 : 0x00;

        // Calculate the exponent using logarithm (log base 2)
        auto exponent = (log (sampleRate) / log (2.0));

        // Add bias to exponent for AIFF
        auto biasedExponent = static_cast<uint16_t> (exponent + 16383);

        // Normalize the sample rate
        auto normalizedSampleRate = sampleRate / pow (2.0, exponent);

        // Calculate the mantissa
        auto mantissa = static_cast<uint64_t> (normalizedSampleRate * (1ULL << 63));

        // Pack the exponent into first two bytes of 10-byte AIFF format
        bytes[0] |= (biasedExponent >> 8) & 0x7F;   // Upper 7 bits of exponent
        bytes[1] = biasedExponent & 0xFF;           // Lower 8 bits of exponent

        // Put the mantissa into byte array
        for (int i = 0; i < 8; ++i) {
            bytes[2 + i] = (mantissa >> (8 * (7 - i))) & 0xFF;
        }
    }
};

class AudioFileAiff : public AudioFile {
protected:
    bool decodeFile(const std::vector<uint8_t>& fileData) override;
    bool procFormatChunk(const std::vector<uint8_t>& fileData) override;
    bool saveToMemory(std::vector<uint8_t>& fileData, AudioFileFormat format) override;
    bool encodeFile(std::vector<uint8_t>& fileData);

    static uint32_t getAiffSampleRate(const std::vector<uint8_t>& fileData, int sampleRateStartIndex) {
        auto sampleRate = AiffUtilities::decodeAiffSampleRate(&fileData[sampleRateStartIndex]);
        return static_cast<uint32_t>(sampleRate);
    }

    static void addSampleRateToAiffData(std::vector<uint8_t>& fileData, uint32_t sampleRateToAdd) {
        std::array<uint8_t, 10> sampleRateData{};
        AiffUtilities::encodeAiffSampleRate (static_cast<double> (sampleRateToAdd), sampleRateData.data());
        fileData.insert(fileData.end(), sampleRateData.begin(), sampleRateData.end());
    }
};

}