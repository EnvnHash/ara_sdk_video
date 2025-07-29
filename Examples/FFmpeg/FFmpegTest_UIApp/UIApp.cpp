//
// Created by user on 24.11.2020.
//

#include "UIApp.h"
#include <VideoView.h>
#include <Conditional.h>

using namespace std;
using namespace ara;

#ifdef ARA_USE_CMRC
#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ara);
#else
namespace fs = std::filesystem;
#endif

UIApp::UIApp() : UIApplication() {
}

void UIApp::init(std::function<void(UINode&)> initCb) {
    // create the main UI-Window. must be on the same thread on that glbase.init happened, in order to have
    // context sharing work
    m_mainWindow = addWindow(UIWindowParams{
            .size           = {1600, 1000},
            .shift          = {50, 20},
            .transparentFB  = false,
    });

    auto vw = m_mainWindow->getRootNode()->addChild<VideoView>();

    //vw->setUrl("rtmp://unstumm.com/live/rs_obs");
    //vw->setUrl("rtmp://unstumm.com/live/rs_obs");
    vw->setAssetName("bunny.mp4"); // loading from a asset in Android
    //vw->setUrl("trailer_1080p.mov");

    vw->setSize(1.f, 1.f);
    vw->setPos(0,0);
    vw->setImgFlags(0);
    vw->setBackgroundColor(1.f, 0.f, 0.f, 1.f);

    startRenderLoop();  // main UIApplication renderloop (for all windows) -> blocking on all OSes but android
}

void UIApp::exit() {
    UIApplication::exit();
}