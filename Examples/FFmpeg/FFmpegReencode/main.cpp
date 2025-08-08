/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */


#include "FFMpeg/FFMpegDecode.h"

using namespace std;
using namespace ara;
using namespace ara::av;

bool			    printFps = true;
bool			    inited = false;
FFMpegDecode        player;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;

void checkFps() {
    dt = glfwGetTime() - actTime;

    // check Framerate every 2 seconds
    if (printFps) {
        double newTimeFmod = std::fmod(glfwGetTime(), 1.0);
        if (newTimeFmod < timeFmod) {
            printf("dt: %f fps: %f \n", dt, 1.0 / dt);
        }
        actTime = glfwGetTime();
        timeFmod = newTimeFmod;
    }
}

int main(int argc, char** argv) {
    player.openFile({ .filePath = "trailer_1080p.mov", .startDecodeThread = true });
    player.start(0.0);

    while (true) {
        checkFps();
    }

    LOG << "bombich";
}
