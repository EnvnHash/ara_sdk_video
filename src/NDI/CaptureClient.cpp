#ifndef __ANDROID__

#include "NDI/CaptureNDI.h"
#include <Processing.NDI.Lib.h>
#include "string_utils.h"

using namespace ara::av::NDI;
using namespace ara;
using namespace std::chrono;

Client::Client(std::string& uname, std::string& ipaddr) : m_source(uname, ipaddr) {
	ResetTimeReference();
}

Client::Client(Source& src) : m_source(src) {
	ResetTimeReference();
}

Client::~Client() {
}

bool Client::Set(std::string& uname, std::string& ipaddr)
{
	m_source.SetName(uname);
	m_source.SetIPAddr(ipaddr);
	m_source.SetUrl(ipaddr); // 57623?

	return true;
}

bool Client::OnCycle()
{
	float vstat_dt;
	uint32_t vstat_fcount = 0;
	auto vstat_ref_time = std::chrono::steady_clock::now();

	LOG << " on cycle connecting to "
        << (m_source.GetName().size() > 1 ? "name " + m_source.GetName() : "")
        << " "
        << (!m_source.GetIPAddr().empty() ? "IP " + m_source.GetIPAddr() : "")
        << " "
        << (!m_source.GetUrl().empty() ? "URL " + m_source.GetUrl() : "");

	ResetTimeReference();

	m_VideoStat_LastFrameOK = false;
	m_VideoStat_FPS = 0;

    NDIlib_recv_create_v3_t create_desc;

    if (m_source.GetName().size() > 1)
        create_desc.source_to_connect_to.p_ndi_name = m_source.GetName().c_str();

    if (m_source.GetIPAddr().size() > 1)
        create_desc.source_to_connect_to.p_url_address = m_source.GetUrl().c_str();

   // create_desc.color_format = NDIlib_recv_color_format_fastest;
    create_desc.color_format = m_colorFormat;

	// create a receiver to look at the source
	m_NDI_Recv = NDIlib_recv_create_v3(&create_desc);

	if (!m_NDI_Recv) {
		m_NDI_Recv = nullptr;
        LOGE << "could not create receiver";
		return false;
	}

    // Connect to our source
    if (!m_source.GetNDISrc())
    {
        // try to find the requested source
        NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
        if (!pNDI_find) return false;

        // Wait until there is one source
        uint32_t no_sources = 0;
        bool found = false;
        const NDIlib_source_t* src = nullptr;
        const NDIlib_source_t* p_sources = nullptr;

        while (!found)
        {	// Wait until the sources on the newtork have changed
            LOG << "Looking for source..." ;
            NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
            p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);

            for (unsigned int i = 0; i < no_sources; i++) {
                src = &p_sources[i];
                LOG << "found source " << src->p_ndi_name << ": " << src->p_ip_address;

                if (m_source.GetName().size() > 1) {
                    auto splStr = split(src->p_ndi_name, "(");
                    if (!splStr.empty()) {
                        auto splStr2 = split(splStr[1], ")");
                        if (!splStr2.empty()) {
                            std::string subStr = splStr2[0].substr(0, m_source.GetName().size());
                            found = subStr == m_source.GetName();
                        }
                    }
                } else if (m_source.GetIPAddr().size() > 1) {
                    //auto splStr = util::split(ns.GetIPAddr(), ":" );
                    found = src->p_ip_address == m_source.GetIPAddr();
                } else {
                    found = true;
                }
            }
         }

        m_source.SetNDISrc(src);
        NDIlib_recv_connect(m_NDI_Recv, src);
        NDIlib_find_destroy(pNDI_find);
    }
    else
    {
        if (m_source.GetNDISrc())
            NDIlib_recv_connect(m_NDI_Recv, m_source.GetNDISrc());
    }

    if (!m_source.GetNDISrc()) {
        LOGE << "Error Could not find ndi_src";
        return false;
    }

    LOG << ">>> PASS 3 "
        << (m_source.GetName().size() > 1 ? "(" + std::string(create_desc.source_to_connect_to.p_ndi_name) + ") " + m_source.GetName() : "");

	//opt_SetTally(1, 1);
	opt_SetAccel(true);

	while (m_CycleState == CycleState::running)
    {
		NDIlib_video_frame_v2_t video_frame;
		NDIlib_audio_frame_v2_t audio_frame;
		NDIlib_metadata_frame_t metadata_frame;

//        auto e = NDIlib_recv_capture_v2(m_NDI_Recv, &video_frame, &audio_frame, &metadata_frame, m_FrameWaitTimeOut_ms);
        auto e = NDIlib_recv_capture_v2(m_NDI_Recv, &video_frame, nullptr, nullptr, m_FrameWaitTimeOut_ms);

		switch (e)
		{

		case NDIlib_frame_type_none: {
			m_VideoStat_LastFrameOK = false;
			m_VideoStat_FPS = 0;
			vstat_fcount = 0;
			//m_glbase->appmsg("no data");
		} break;

		case NDIlib_frame_type_video: {
            //m_glbase->appmsg("NDIlib_frame_type_video");

            m_VideoStat_LastFrameOK = true;

			if (m_frameWidth == 0 || m_frameWidth != video_frame.xres)
				m_frameWidth = video_frame.xres;

			if (m_frameHeight == 0 || m_frameHeight != video_frame.yres)
				m_frameHeight = video_frame.yres;

			ProcessVideoFrame(&video_frame);

			// Video Statistics

			auto ct = std::chrono::steady_clock::now();

			if (!vstat_fcount) vstat_ref_time = ct;

			if ((vstat_dt = std::chrono::duration<float>(ct - vstat_ref_time).count()) >= m_VideoStat_Period_ms * 1e-3) {

				m_VideoStat_FPS = vstat_fcount / vstat_dt;
				vstat_fcount = 0;
				vstat_ref_time = ct;
			}

			vstat_fcount++;

			NDIlib_recv_free_video_v2(m_NDI_Recv, &video_frame);
		} break;

		case NDIlib_frame_type_audio: {
			NDIlib_recv_free_audio_v2(m_NDI_Recv, &audio_frame);
		} break;

		case NDIlib_frame_type_metadata: {
			//m_glbase->appmsg("Meta data (%s)",metadata_frame.p_data);
			NDIlib_recv_free_metadata(m_NDI_Recv, &metadata_frame);
		} break;

		case NDIlib_frame_type_error: {
			LOGE << "NDIlib_frame_type_error";
		} break;

		case NDIlib_frame_type_status_change: {
		} break;

		default:
			break;
		}
	}

	NDIlib_recv_destroy(m_NDI_Recv);

	return true;
}

bool Client::ProcessVideoFrame(NDIlib_video_frame_v2_t* vframe)
{
	m_Video_ExpectedFPS = vframe->frame_rate_D > 0 ? (float)vframe->frame_rate_N / (float)vframe->frame_rate_D : 0;
	m_FrameLastTime = system_clock::now();
	OnFrameArrive(vframe);
    if (m_newFrameCb)
        m_newFrameCb();
	return false;
}

bool Client::opt_SetAccel(bool on_off)
{
	if (!IsRunning()) return false;
	NDIlib_metadata_frame_t enable_hw_accel;
	enable_hw_accel.p_data = on_off ? (char*)"<ndi_hwaccel enabled=\"true\"/>" : (char*)"<ndi_hwaccel enabled=\"false\"/>";
	NDIlib_recv_send_metadata(m_NDI_Recv, &enable_hw_accel);

	return true;
}

bool Client::opt_SetTally(bool program, bool preview)
{
	if (!IsRunning()) return false;

	NDIlib_tally_t tally_state;
	tally_state.on_program = program;
	tally_state.on_preview = preview;
	NDIlib_recv_set_tally(m_NDI_Recv, &tally_state);

	return true;
}

double Client::GetSecondsSinceLastFrame()
{
	duration<double> diff = system_clock::now() - m_FrameLastTime;
	return diff.count();
}

void Client::ResetTimeReference() {
	m_FrameLastTime = std::chrono::system_clock::now();
	//m_FrameLastTime -=100;
}

#endif