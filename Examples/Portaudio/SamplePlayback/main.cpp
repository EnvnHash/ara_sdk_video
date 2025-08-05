//
// Created by sven on 04-08-25.
//

#include "Portaudio/Portaudio.h"
#include "AudioFile/AudioFileWav.h"

using namespace ara;
using namespace ara::av;
using namespace std;

int main(int argc, char** argv) {
    Conditional running;
    AudioFileWav audioFile;

    try {
        audioFile.load("stereol.wav", SampleOrder::Interleaved);
        audioFile.printSummary();
    } catch (const std::exception& ex) {
        LOGE << "Error: " << ex.what();
        return 1;
    }

    Portaudio pa;
    pa.init({
        .sampleRate = static_cast<int32_t>(audioFile.getSampleRate()),
        .numChannels = audioFile.getNumChannels(),
        .useCycleBuffer = false
    });

    int32_t samplePtr = 0;
    bool playInLoop = true;

    // set a callback to be called after each portaudio stream callback finish
    pa.setStreamProcCb([&] (const void* inBuf, void* outBuf, uint64_t framesPerBuf){
        unique_lock<mutex> l(pa.getStreamMtx());
        auto samples = audioFile.getSamplesInterleaved();
        auto range = framesPerBuf * audioFile.getNumChannels();
        auto endSamp = samplePtr + range;
        auto endSampLimited = std::min(endSamp, samples.size() - 1);

        std::copy(samples.begin() + samplePtr, samples.begin() + endSampLimited, reinterpret_cast<float*>(outBuf));

        if (endSamp > samples.size() && playInLoop) {
            std::copy(samples.begin(),
                      samples.begin() + (endSamp - endSampLimited),
                      reinterpret_cast<float*>(outBuf) + endSampLimited);
        }
        samplePtr = (samplePtr + range) % samples.size();
    });

    pa.start();
    running.wait(5000); // play for 5 sec
    pa.stop();

    return 0;
}