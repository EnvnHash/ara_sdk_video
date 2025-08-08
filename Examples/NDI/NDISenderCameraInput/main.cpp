/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */


#include "FFMpeg/FFMpegPlayer.h"
#include <WindowManagement/GLFWWindow.h>
#include <GeoPrimitives/Quad.h>
#include <GLBase.h>
#include <Processing.NDI.Lib.h>
#include <libyuv.h>

using namespace std;
using namespace ara::glb;
using namespace ara::cap;

bool			    printFps = false;
bool			    inited = false;

GLFWwindow*		    window = nullptr;
GLBase              glbase;
Shaders*            stdTex;
unique_ptr<Quad>    quad;
FFMpegDecode        decoder;
unique_ptr<Texture> test_png;
int				    winWidth = 1280;
int				    winHeight = 800;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;

NDIlib_send_create_t NDI_send_create_desc;
NDIlib_send_instance_t pNDI_send;
NDIlib_metadata_frame_t NDI_connection_type;
NDIlib_video_frame_v2_t NDI_video_frame;

static void output_error(int error, const char* msg)
{
	fprintf(stderr, "Error: %s\n", msg);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
	    decoder.stop();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

void init()
{
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glbase.init(false);
    glfwMakeContextCurrent(window);

    test_png = make_unique<Texture>(&glbase);
    test_png->keepBitmap(true);
    test_png->loadTexture2D("resdata/FullHD_Pattern.png");

    // stdTex = m_shCol.getStdTex();
    quad = make_unique<Quad>(-1.f, -1.f, 2.f, 2.f,
                             glm::vec3(0.f, 0.f, 1.f),
                             1.f, 0.f, 0.f, 1.f,
                             nullptr, 1, true);  // create a Quad, standard width and height (normalized into -1|1), static red
#ifdef _WIN32
    decoder.OpenCamera(&glbase, "USB2.0 HD UVC WebCam", winWidth, winHeight, false);
#elif __linux__
    decoder.openCamera(&glbase, "/dev/video0", winWidth, winHeight, false);
#endif

    decoder.setVideoUpdtCb([&](AVFrame* image_data){
        // AV_PIX_FMT_YUYV422 -> packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
       // if (image_data->format == AV_PIX_FMT_YUYV422)
        //{
/*
            libyuv::YUY2ToI422(&image_data->data[0][0],  image_data->linesize[0])
            uint8_t* dst_y,
            int dst_stride_y,
            uint8_t* dst_u,
            int dst_stride_u,
            uint8_t* dst_v,
            int dst_stride_v,
            int width,
            int height);
*/
            NDI_video_frame.xres = image_data->width;
            NDI_video_frame.yres = image_data->height;
            NDI_video_frame.FourCC = NDIlib_FourCC_video_type_UYVY;
            NDI_video_frame.line_stride_in_bytes = image_data->linesize[0];
            NDI_video_frame.p_data = &image_data->data[0][0];
/*
            NDI_video_frame.xres = test_png->getWidth();
            NDI_video_frame.yres = test_png->getHeight();
            NDI_video_frame.FourCC = NDIlib_FourCC_type_BGRX;
            //NDI_video_frame.line_stride_in_bytes = test_png->getWidth() * 4;
            NDI_video_frame.p_data = test_png->getBits();
*/
            NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);
     //   }
    });


    decoder.start(glfwGetTime());
}

static void display()
{
	dt = glfwGetTime() - actTime;

	// check Framerate every 2 seconds
	if (printFps)
	{
		double newTimeFmod = std::fmod(glfwGetTime(), 2.0);
		if (newTimeFmod < timeFmod)
		{
			printf("dt: %f fps: %f \n", dt, 1.0 / dt);
		}
		actTime = glfwGetTime();
		timeFmod = newTimeFmod;
	}

    if (!inited){
        init();
        inited = true;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    decoder.loadFrameToTexture(glfwGetTime());
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    decoder.shaderBegin(); // draw with conversion yuv -> rgb on gpu
    quad->draw();
    // decoder.shaderEnd(); // draw with conversion yuv -> rgb on gpu

	glfwSwapBuffers(window);
}

int main(int argc, char** argv)
{
    if (!NDIlib_initialize())
    {
        printf("[ERROR] Cannot run NDI.\n");
        return 0;
    }

    // Create an NDI source that is clocked to the video.
    NDI_send_create_desc.p_ndi_name = "test_cam";

    pNDI_send = NDIlib_send_create(&NDI_send_create_desc);

    if (!pNDI_send) {
        printf("[ERROR] Cannot create NDIlib_send_instance_t\n");
        return -1;
    }
/*
    NDI_connection_type.p_data = (char*)"<ndi_product long_name=\"NDI Server\" "
                                        "             short_name=\"NDI\" "
                                        "             manufacturer=\"ara\" "
                                        "             version=\"0.5.0\" "
                                        "             session=\"default\" "
                                        "             model_name=\"CAL1\" "
                                        "             serial=\"000000\"/>";

    NDIlib_send_add_connection_metadata(pNDI_send, &NDI_connection_type);
*/
    // -----------------------------------------------------

    glfwSetErrorCallback(output_error);

	if (!glfwInit())
		LOGE << "Failed to initialize GLFW";

 	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
	glfwWindowHint(GLFW_DECORATED, GL_TRUE);

	window = glfwCreateWindow(winWidth, winHeight, "Warping Tool", NULL, NULL);
	if (!window) {
		LOGE << "Failed to create GLFW window";
		glfwTerminate();
	}

	glfwSetKeyCallback(window, key_callback);
	glfwWindowHint(GLFW_SAMPLES, 2);
	glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);
	glfwSetWindowPos(window, 0, 0);
	glfwShowWindow(window);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	LOG << "Vendor:   " << glGetString(GL_VENDOR);
	LOG << "Renderer: " << glGetString(GL_RENDERER);
	LOG << "Version:  " << glGetString(GL_VERSION);
	LOG << "GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION);

    ara::glb::initGLEW();

    glViewport(0, 0, winWidth, winHeight);

    while (!glfwWindowShouldClose(window))
	{
		display();
        glfwPollEvents();
    }

    NDIlib_send_destroy(pNDI_send);
    NDIlib_destroy();

    LOG << "bombich";
}
