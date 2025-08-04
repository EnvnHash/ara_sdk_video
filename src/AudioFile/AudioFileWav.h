//
// Created by sven on 04-08-25.
//

#pragma once

#include "AudioFile/AudioFile.h"

namespace ara::av {

class AudioFileWav : public AudioFile {
    bool loadFromMemory(const std::vector<uint8_t>& fileData) override;
    bool decodeFile (const std::vector<uint8_t>& fileData);
    bool saveToMemory(std::vector<uint8_t>& fileData, AudioFileFormat format) override;
    bool encodeFile (std::vector<uint8_t>& fileData);
};

}