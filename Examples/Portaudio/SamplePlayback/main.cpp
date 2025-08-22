//
// Created by sven on 04-08-25.
//

#include "Portaudio/PortaudioAudioEngine.h"

using namespace ara;
using namespace ara::av;
using namespace std;

int main(int argc, char** argv) {
    Conditional running;

    PortaudioAudioEngine pa;
    pa.init({
        .numChannels = 2,
        .allocateBuffers = 5
    });

    auto& sine = pa.loadAudioFile("sine.wav");
    sine.setLooping(true);
    sine.printInfo();

    auto& sample = pa.loadAudioFile("stereol.wav");
    sample.printInfo();

    pa.start();
    pa.printInfo();

    std::thread([&]{
        std::this_thread::sleep_for(2s);
        pa.play(sample);
    }).detach();

    pa.play(sine);
    running.wait(5000); // play for 5 sec
    pa.stop();

    return 0;
}