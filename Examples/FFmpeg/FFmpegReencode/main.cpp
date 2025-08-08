/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */


#include "FFMpeg/FFMpegDecode.h"
#include <StopWatch.h>

using namespace std;
using namespace ara;
using namespace ara::av;

FFMpegDecode        player;
StopWatch           watch;

int main(int argc, char** argv) {
    player.openFile({ .filePath = "trailer_1080p.mov", .startDecodeThread = true });

    while (true) {
        watch.setStart();
        watch.setEnd();
        watch.print("decode time: ");
    }

    LOG << "bombich";
}
