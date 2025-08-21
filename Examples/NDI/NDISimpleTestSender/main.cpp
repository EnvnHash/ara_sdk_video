#include <stdlib.h>
#include <chrono>
#include <thread>
#include <Network/NetworkCommon.h>

#ifdef _WIN32

#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64

#include <windows.h>

#endif

// PNG loader in a single file !
// From http://lodev.org/lodepng/ 
#include "picopng.hpp"
#include <string.h>
#include <Processing.NDI.Lib.h>
#include <Network/UDPSender.h>
#include <Network/UDPReceiver.h>

using namespace std;
using namespace ara;

std::string								g_StationName = { "Test-NDIServer" };

UDPSender								g_UDPSignaler;
int										g_UDPSignaler_Port=18072;

UDPReceiver								g_UDPListener;
int										g_UDPListener_Port=18072;
int										g_UDPSignaler_Period_ms=1000;

vector<string>							g_RemoteDisplayName;

std::vector<std::string> SplitString(std::string s, std::string delimiter) {

	size_t pos_start = 0, pos_end, delim_len = delimiter.length();
	string token;
	vector<string> res;

	while ((pos_end = s.find (delimiter, pos_start)) != string::npos)
	{
		token = s.substr (pos_start, pos_end - pos_start);
		pos_start = pos_end + delim_len;
		res.push_back (token);
	}

	res.push_back (s.substr (pos_start));
	return res;
}

void NDI_Send_Sample(std::string ndi_name) {

	printf("Start NDI sender (%s)\n",ndi_name.c_str());

	std::vector<unsigned char> png_data;
	loadFile(png_data, "FullHD_Pattern.png");

	if (png_data.empty()) {
		printf("[ERROR] PNG empty\n");
		return;
	}

	// Decode the PNG file
	std::vector<uint8_t> image_data;
	unsigned long xres = 0, yres = 0;

	if (decodePNG(image_data, xres, yres, &png_data[0], png_data.size(), true)) {
		printf("[ERROR] Cannot decode PNG\n");
		return;
	}

	// Create an NDI source that is called "My PNG" and is clocked to the video.
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.p_ndi_name = ndi_name.c_str();

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);

	if (!pNDI_send) {
		printf("[ERROR] Cannot create NDIlib_send_instance_t\n");
		return;
	}

	NDIlib_metadata_frame_t NDI_connection_type; 
	NDI_connection_type.p_data = (char*)"<ndi_product long_name=\"NDI Server\" "
		"             short_name=\"NDI\" "
		"             manufacturer=\"ara\" "
		"             version=\"0.5.0\" "
		"             session=\"default\" "
		"             model_name=\"CAL1\" "
		"             serial=\"000000\"/>";

	NDIlib_send_add_connection_metadata(pNDI_send, &NDI_connection_type);

	// We are going to create a m_frame
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.xres = xres;												  
	NDI_video_frame.yres = yres;
	NDI_video_frame.FourCC = NDIlib_FourCC_type_RGBA;
	NDI_video_frame.p_data = &image_data[0];
	NDI_video_frame.line_stride_in_bytes = xres * 4;

	printf("NDI Sender (%s) : %dx%d\n", NDI_send_create_desc.p_ndi_name, NDI_video_frame.xres, NDI_video_frame.yres);

	using namespace std::chrono;
	int idx,cf=0;

	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);)
	{	// Get the current time
		const auto start_send = high_resolution_clock::now();

		// Send 200 frames
		for (idx = 0; idx<200; idx++,cf++)
		{	

			int pos=(cf%yres)*NDI_video_frame.line_stride_in_bytes;
			uint8_t* b=NDI_video_frame.p_data+pos;
			unsigned long i;

			for (i=0; i<xres*4; i+=4){ b[i]^=255; b[i+1]^=255; b[i+2]^=255; b[i+3]=255; }

			NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
		}

		// Just display something helpful
		printf("(%s) %d frames sent, at %1.2ffps\n", ndi_name.c_str(),idx, (float) idx / duration_cast<duration<float>>(high_resolution_clock::now() - start_send).count());

	}

	NDIlib_send_destroy(pNDI_send);
	printf("NDI sender finished (%s)\n",ndi_name.c_str());
}

bool Start_Comm(char* name) {

	// Here we should read the configuration for the machine... from a file?
    char hostname[256];
    gethostname(hostname, 256);
    g_StationName = hostname;

	g_UDPListener_Port=18072;			// Should be read from a file
	g_UDPSignaler_Port=18072;
	g_UDPSignaler_Period_ms=1000;

	// Display data...
	printf("Station name         : %s\n",g_StationName.c_str());
	printf("Listening on port    : %d\n",g_UDPListener_Port);
	printf("Signaling on port    : %d (every %d ms)\n",g_UDPSignaler_Port,g_UDPSignaler_Period_ms);

    std::thread(NDI_Send_Sample,name).detach();

    return true;
}

int main(int argc, char* argv[])
{
	if (!NDIlib_initialize())
	{
		printf("[ERROR] Cannot run NDI.\n");
		return 0;
	}	

	if (argc <= 1){
	    printf("Missing Arguments. Please provide a channel name argument. E.m_g. ./NDSimpleTestSender DISPLAY1");
	    exit(0);
	}

	Start_Comm(argv[1]);
	printf("\n\n[WARNING] SAMPLE> Running for 2 min...\n\n");

    chrono::duration<double> ever(numeric_limits<double>::infinity());
    this_thread::sleep_for(ever);

	NDIlib_destroy();

	return 0;
}

