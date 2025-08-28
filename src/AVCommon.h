//
// Created by user on 14.08.2021.
//

#pragma once

#include <util_common.h>

struct AVPacket;

namespace ara::av {

class audioCbData {
public:
    uint32_t nChannels=0;
    uint32_t samples=0;
    uint32_t byteSize=0;
    uint8_t** buffer=nullptr;
    uint32_t sampleRate=0;
    int32_t sampleFmt=0;
    double ptss=0;
    AVPacket* packet=nullptr;
};

}