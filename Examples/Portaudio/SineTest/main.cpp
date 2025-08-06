//
// Created by sven on 04-08-25.
//

#include "Portaudio/Portaudio.h"

using namespace ara;
using namespace ara::av;
using namespace std;

int main(int argc, char** argv) {
    Conditional running;
    std::array<double, 200> sinTable{};
    for (auto i=0; i<sinTable.size(); ++i) {
        sinTable[i] = std::sin(static_cast<double>(i) / static_cast<double>(sinTable.size()) * M_PI * 2.0);
    }

    Portaudio pa;
    pa.init({ .allocateBuffers = 3 }); // deduces the actual audio hardware configuration

    // init cycle buffer
    std::deque<size_t> phase(pa.getNrOutChannels());
    std::ranges::fill(phase, 0);

    // set a callback to be called after each portaudio stream callback finish
    pa.setStreamProcCb([&](const void*, void*, uint64_t) {
        unique_lock<mutex> l(pa.getStreamMtx());
        auto bufPtr = pa.getCycleBuffer().getWriteBuff().getDataPtr();
        for (auto frame=0; frame<pa.getFramesPerBuffer(); ++frame) {
            for (auto chan=0; chan<pa.getNrOutChannels(); ++chan) {
                *bufPtr++ = static_cast<float>(sinTable[phase[chan] % sinTable.size()]);
                phase[chan] += chan+1; // different pitch for left and right
            }
        }
        pa.getCycleBuffer().feedCountUp();
    });

    pa.start();

    running.wait(5000); // play for 5 sec

    pa.stop();

    return 0;
}