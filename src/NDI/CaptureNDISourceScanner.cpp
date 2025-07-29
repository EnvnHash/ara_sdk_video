#ifndef __ANDROID__

#include "CaptureNDI.h"
#include <string_utils.h>

using namespace ara::av::NDI;
using namespace ara;

namespace ara::av::NDI {

size_t SourceScanner::GetCount()        { return m_Sources.size(); }

Source SourceScanner::Get(size_t index) { return m_Sources.at(index); }

bool SourceScanner::Start(char* chanName, char* ip) {
    //std::unique_lock<std::mutex> lck (m_Cycle_Mutex);

    if (chanName) {
        m_searchStr = std::string(chanName);
    }

    if (ip) {
        m_searchIp = std::string(ip);
    }

    if (!(m_CycleState==CycleState::none || m_CycleState==CycleState::finished || m_CycleState>=CycleState::err_failed)) {
        return false;
    }

    m_CycleState = CycleState::starting;
    m_Cycle_Thread = std::thread(&Cycler::Cycle,this);
    m_Cycle_Thread.detach();

    return true;
}

bool SourceScanner::OnCycle() {
    LOG << "Start> SourceScanner::OnCycle()";

    // create a finder
    NDIlib_find_create_t find_desc;
    find_desc.show_local_sources = true;
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2(&find_desc);

    m_Sources.clear();

    if (!pNDI_find) {
        return false;
    }

    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = nullptr;
    unsigned long i;
    bool isValidDisplay = false;

    // check for new NDI Sources, only take those with the name m_searchStr
    while (m_CycleState == CycleState::running) {
        NDIlib_find_wait_for_sources(pNDI_find, m_WaitForSourcesTimeOut_ms);
        p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

        for (i = 0; i < no_sources; i++) {
            Source ns(&p_sources[i]);

            if (!m_searchStr.empty()) {
                auto splStr = split(ns.GetName(), "(" );
                if (splStr.size() >= 1){
                    auto splStr2 = split(splStr[1], ")");
                    if (splStr2.size() >= 1){
                        std::string subStr = splStr2[0].substr(0,m_searchStr.size());
                        isValidDisplay = subStr == m_searchStr;
                    }
                }
            } else if (!m_searchIp.empty()) {
                //auto splStr = util::split(ns.GetIPAddr(), ":" );
                isValidDisplay = ns.GetIPAddr() == m_searchIp;
            } else {
                isValidDisplay = true;
            }

            if (isValidDisplay && std::find_if(m_Sources.begin(), m_Sources.end(), [&ns](const Source& s) {return s == ns; }) == m_Sources.end()) {
                LOG << "Adding " << ns.GetName() << " @ " << ns.GetIPAddr() << " and notifying";
                m_Sources.push_back(ns);

                if (m_newSourceCB) {
                    m_newSourceCB(&ns);
                }
            }
        }
    }

    NDIlib_find_destroy(pNDI_find);
    LOG << "Finish> SourceScanner::OnCycle()";

    return true;
}

}

#endif