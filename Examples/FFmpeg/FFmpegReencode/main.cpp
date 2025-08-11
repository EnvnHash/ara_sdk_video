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

FFMpegDecode        decoder;
StopWatch           fpsWatch;
StopWatch           decodeWatch;

int main(int argc, char** argv) {
    bool run = true;
    decodeWatch.setStart();

    decoder.openFile({ .filePath = "trailer_1080p.mov", .startDecodeThread = true });
    decoder.setEndCbFunc([&]{
       run = false;
    });

    while (run) {
        fpsWatch.setStart();
        fpsWatch.setEnd();
        fpsWatch.print("decode time: ");

        auto b = decoder.reqNextBuf();
        if (b) {
            //LOG << "got buf";
        }
    }

    decodeWatch.setEnd();
    LOG << "bombich! decode took: " << decodeWatch.getDt() << " ms";
}
