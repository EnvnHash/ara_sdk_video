/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */


#include "FFMpeg/FFMpegPlayer.h"
#include "Portaudio/PortaudioAudioEngine.h"

using namespace std;
using namespace ara;
using namespace ara::av;

FFMpegPlayer        player;
PortaudioAudioEngine pa;

int main(int argc, char** argv) {
    bool run = true;
    pa.init({
        .sampleRate = 44100,
        .numChannels = 2,
        .allocateBuffers = 18
    });

    player.openFile({
        .portaudioEngine = &pa,
        .filePath = "main_menu_2.mp3"
    });
    player.start(0.0);

    while (run) {
        std::this_thread::sleep_for(500ms);
    }

    LOG << "bombich";
}
