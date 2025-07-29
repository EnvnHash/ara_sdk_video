//
// Created by user on 27.04.2021.
//

#include "pch.h"
#include <Conditional.h>
#include <IntelRealsense/IntelRealsense.h>

#include "gmock/gmock.h"

using namespace std;
using namespace ara::util;
using namespace ara::av;


TEST(VideoInput_UnitTests, ConditionalTest)
{
    Conditional m_grabCond;
    glm::ivec2 m_rsRes=glm::ivec2{1280,720};
    int nrLoops = 1;
    int nrDown = 0;
    bool didFindMemLeak=false;

#ifdef _WIN32
    _CrtMemState sOld, sNew, sDiff;
    _CrtMemCheckpoint(&sOld); //take a snapshot
#endif

    for (int j=0; j<nrLoops; j++)
    {
        rs::System m_rsSystem;
        rs::Device* m_rsDev=nullptr;

        LOG << "m_rsSystem.init()";
        if (!m_rsSystem.init())
        {
            LOGE << " Intel Realsense init failed!!!!";
            EXPECT_TRUE(false);
            return;
        }
        else
        {
            LOG << "m_rsSystem.init() done! ";

            if (!m_rsDev)
                m_rsDev = m_rsSystem.getFirstDevice();

            m_rsDev->start(m_rsRes.x, m_rsRes.y, 30, 2, [this, &nrDown, &m_grabCond](rs::e_frame* frame){
                nrDown++;
                if (nrDown == 3)
                    m_grabCond.notify();
            });

            m_grabCond.wait();
            m_rsDev->stop();

        }
    }

#ifdef _WIN32
    _CrtMemCheckpoint(&sNew); //take a snapchot
    if (_CrtMemDifference(&sDiff, &sOld, &sNew)) // if there is a difference
    {
        LOG << "sDiff.lTotalCount " << sDiff.lTotalCount;
        didFindMemLeak = true;
    }
#endif

    EXPECT_FALSE(didFindMemLeak);

    LOG << "done";
}