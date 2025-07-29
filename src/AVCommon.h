//
// Created by user on 14.08.2021.
//

#pragma once

#include <algorithm>
#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <iomanip>

namespace ara::av
{

class audioCbData
{
public:
    uint32_t nChannels=0;
    uint32_t samples=0;
    uint32_t byteSize=0;
    //float** buffer=nullptr;
    uint8_t** buffer=nullptr;
    uint32_t sampleRate=0;
    int32_t sampleFmt=0;
};

}