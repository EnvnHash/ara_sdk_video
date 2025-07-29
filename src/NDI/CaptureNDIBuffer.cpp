#ifndef __ANDROID__

#include "CaptureNDI.h"

using namespace ara::av::NDI;
using namespace ara;
using namespace std;
namespace fs = std::filesystem;

BufferClient::BufferClient(std::string& uname, std::string ipaddr) : Client(uname,ipaddr) {
}

BufferClient::BufferClient(Source& src) : Client(src) {
}

bool BufferClient::AllocateIBuffers()
{
	iBuff.clear();

	for (int i=0; i<4; i++)
		iBuff.push_back(make_unique<CIBuff>());

	return true;
}

bool BufferClient::OnFrameArrive(NDIlib_video_frame_v2_t* vframe)
{
	if (iBuff.size() == 0) AllocateIBuffers();

	if (iBuff.at(iBuffPos)->Feed(vframe))
    {
		iBuffLastPos=iBuffPos;
		iBuffPos++;
		iBuffPos %= iBuff.size();
	}


	return true;
}

BufferClient::CIBuff* BufferClient::GetLastBuff() {

	if (iBuffLastPos<0) return nullptr;
	return iBuff[iBuffLastPos].get();
}

// maintain a number of buffers on the CPU ? why not directly on the GPU and save the performance?
// (marco.m_g) A> BufferClient should be kept independently of GL and the image m_buffer could be useful for something else,
//              by now we just need 2 images only, since could be that while uploading to GPU we could be receiving a new image

bool BufferClient::CIBuff::Feed(NDIlib_video_frame_v2_t* vframe) {

	if (vframe->FourCC==NDIlib_FourCC_video_type_UYVY
        || vframe->FourCC==NDIlib_FourCC_video_type_UYVA)
    {
        size_t tbytes=vframe->xres*vframe->yres*2;		// 3*4/6

        if (tbytes<=0) return false;

        if (tbytes > img.size()) img.resize(tbytes);

        pixSize[0]=vframe->xres;
        pixSize[1]=vframe->yres;

        uint8_t* src=(uint8_t*) vframe->p_data;
        auto dest= &img[0];
        int dbx=pixSize[0]*2;

        for (int i = 0; i < pixSize[1]; i++, src += vframe->line_stride_in_bytes, dest += dbx)
            memcpy(dest,src,dbx);
        //std::copy(src, (uint8_t*)(src + vframe->line_stride_in_bytes), dest);

        return true;

    } else {
        return false;
    }
}

#endif