/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */

#include "Portaudio/PortaudioAudioEngine.h"

using namespace std;
using namespace ara;
using namespace ara::av;

PortaudioAudioEngine pa;

int main(int, char**) {
    Conditional running;
    pa.init({
        .sampleRate = 44100,
        .numChannels = 2,
        .allocateBuffers = 3,
        .framesPerBuffer = 128
    });

    auto& af = pa.loadAudioFile("main_menu_2.mp3");
    af.setLooping(true);

    pa.start();
    pa.play(af);
    running.wait(20000); // play for 5 sec
    pa.stop();

    LOG << "bombich";
}
