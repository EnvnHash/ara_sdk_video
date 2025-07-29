#ifndef __ANDROID__

#include "CaptureNDI.h"

using namespace ara::av::NDI;

namespace ara::av::NDI{
    bool operator==(const Source& s, const Source& d) {
        return s.GetName()==d.GetName() && s.GetIPAddr()==d.GetIPAddr();
    }
}

Source::Source(const NDIlib_source_t* ndi_src) {

	if (ndi_src->p_ndi_name != nullptr) {
		m_uName = ndi_src->p_ndi_name;
	}

	if (ndi_src->p_ip_address!=nullptr)
	    m_IPAddr=ndi_src->p_ip_address;

    if (ndi_src->p_url_address!=nullptr)
        m_Url=ndi_src->p_url_address;

    m_ndi_src = ndi_src;
}

#endif