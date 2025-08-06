//
// Created by sven on 04-08-25.
//

#include "Portaudio/PortaudioAudioEngine.h"
#include "AudioFile/Sample.h"

using namespace ara;
using namespace ara::av;
using namespace std;

int main(int argc, char** argv) {
    Conditional running;

    PortaudioAudioEngine pa;
    pa.init({
        .numChannels = 2,
        .allocateBuffers = 3
    });

    auto& sample = pa.loadSample("stereol.wav");
    sample.setLooping(true);
    sample.printInfo();

    pa.start();
    pa.printInfo();

    pa.play(sample);
    running.wait(5000); // play for 5 sec
    pa.stop();

    return 0;
}