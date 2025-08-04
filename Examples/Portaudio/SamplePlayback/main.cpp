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
        audioFile.load ("stereol.wav");
        LOG << "Loaded WAV file:";
        LOG << " - Channels: " << audioFile.getNumChannels();
        LOG << " - Sample Rate: " << audioFile.getSampleRate() << " Hz";
        LOG << " - Bits per Sample: " << audioFile.getBitDepth();
        LOG << " - length in sec: " << audioFile.getLengthInSeconds() << " sec";
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

    // set a callback to be called after each portaudio stream callback finish
    pa.setStreamProcCb([&] (const void* inBuf, void* outBuf, uint64_t framesPerBuf){
        unique_lock<mutex> l(pa.getStreamMtx());
        auto outBufPtr = reinterpret_cast<float*>(outBuf);
        for (auto frame = 0; frame < framesPerBuf; ++frame) {
            for (int chan=0; chan<pa.getNrOutChannels(); ++chan) {
                *outBufPtr++ = audioFile.getSample(chan, samplePtr);
            }
            samplePtr = ++samplePtr % audioFile.getNumSamplesPerChannel();
        }
    });

    pa.start();

    running.wait(5000); // play for 5 sec

    pa.stop();

    return 0;
}