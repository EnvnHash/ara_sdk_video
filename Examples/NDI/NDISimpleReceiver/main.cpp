/*
 * main.cpp
 *
 *  Created on: Jun 20, 2022
 *      Author: sven
 */


#include <WindowManagement/GLFWWindow.h>
#include <GeoPrimitives/Quad.h>
#include <GLBase.h>
#include <Processing.NDI.Lib.h>

using namespace std;
using namespace ara::glb;

GLFWwindow*		    window = nullptr;
GLBase              glbase;
ShaderCollector     shCol;
Shaders*            stdTex;
unique_ptr<Quad>    quad;
unique_ptr<Texture> m_tex;

bool			    printFps = false;
bool			    inited = false;
bool                m_texInited = false;
int				    winWidth = 1280;
int				    winHeight = 800;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;

NDIlib_video_frame_v2_t video_frame;
NDIlib_audio_frame_v2_t audio_frame;
const NDIlib_source_t* p_sources = nullptr;
NDIlib_recv_instance_t pNDI_recv;
vector<uint8_t> img;
NDIlib_recv_create_v3_t recv_desc;
NDIlib_recv_color_format_e recv_col_format = NDIlib_recv_color_format_fastest;

static void output_error(int error, const char* msg)
{
	fprintf(stderr, "Error: %s\n", msg);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

Shaders* initRenderShdr()
{
    string vert = shCol.getShaderHeader();
    vert += STRINGIFY(
        layout(location = 0) in vec4 position; \n
        layout(location = 2) in vec2 texCoord;
        uniform sampler2D tex;
        out vec2 tex_coord; \n
        void main() { \n
            ivec2 tsize = textureSize(tex, 0);
            tex_coord = vec2(texCoord.x * tsize.x, texCoord.y * tsize.y); \n
            gl_Position = position; \n
    });

    string frag = shCol.getShaderHeader();
    frag += STRINGIFY(
        layout(location = 0) out vec4 fragColor; \n
        in vec2 tex_coord; \n
        uniform sampler2D tex;
        void main()\n
        { \n
            ivec2 a = ivec2(tex_coord);
            vec3 ycbcr;
            vec4 texCol = texelFetch(tex, a, 0);

            ycbcr.x = texCol.r;
            ycbcr.y = ((a.x & 1) == 0 ? texCol.g : texelFetch(tex, ivec2(a.x - 1, a.y), 0).g) - 0.5;
            ycbcr.z = ((a.x & 1) == 0 ? texelFetch(tex, ivec2(a.x + 1, a.y), 0).g : texCol.g) - 0.5;

            fragColor = vec4(ycbcr.x + (1.4065 * (ycbcr.z)),
                ycbcr.x - (0.3455 * (ycbcr.y)) - (0.7169 * (ycbcr.z)),
                ycbcr.x + (1.7790 * (ycbcr.y)), texCol.a);
        }
    );

    return shCol.add("NDIRender_UYVY_cam_input", vert.c_str(), frag.c_str());
}

void init()
{
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glbase.init(false);
    glfwMakeContextCurrent(window);

    //stdTex = shCol.getStdTex();
    stdTex = initRenderShdr();

    quad = make_unique<Quad>(-1.f, -1.f, 2.f, 2.f,
                             glm::vec3(0.f, 0.f, 1.f),
                             1.f, 0.f, 0.f, 1.f,
                             nullptr, 1, true);  // create a Quad, standard width and height (normalized into -1|1), static red
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

    postGLError();

    switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, &audio_frame, nullptr, 5000))
    {	// No data
        case NDIlib_frame_type_none:
            std::cout <<  "No data received." << std::endl;
            break;

            // Video data
        case NDIlib_frame_type_video:
        {
            //std::cout << "Video data received (" <<  video_frame.xres << "x" << video_frame.yres << ")" << std::endl;
            if (!m_texInited)
            {
                if (video_frame.FourCC == NDIlib_FourCC_video_type_UYVY)
                    LOG << "NDIlib_FourCC_video_type_UYVY";
                else if (video_frame.FourCC == NDIlib_FourCC_video_type_BGRX)
                    LOG << "NDIlib_FourCC_video_type_BGRX";
                else if (video_frame.FourCC == NDIlib_FourCC_video_type_RGBX)
                    LOG << "NDIlib_FourCC_video_type_RGBX";
                else if (video_frame.FourCC == NDIlib_FourCC_video_type_RGBA)
                    LOG << "NDIlib_FourCC_video_type_RGBA";

                if (recv_col_format == NDIlib_recv_color_format_fastest)
                {
                    m_tex = make_unique<Texture>(&glbase);
                    m_tex->allocate2D(video_frame.xres, video_frame.yres, GL_RG8, GL_RG, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);
                    img.resize(video_frame.xres * video_frame.yres*2);
                    m_texInited = true;
                }
            }

            if (m_tex)
            {
                if (recv_col_format == NDIlib_recv_color_format_fastest)
                {
                    auto src = (uint8_t *) video_frame.p_data;
                    memcpy(&img[0], src, video_frame.xres * video_frame.yres * 2);
                }

                m_tex->upload((void*)&img[0]);
            }

            NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
        }
        break;

        // Audio data
        case NDIlib_frame_type_audio:
            printf("Audio data received (%d samples).\n", audio_frame.no_samples);
            NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
            break;

        case NDIlib_frame_type_error:
            std::cout <<  "NDIlib_frame_type_error." << std::endl;
            break;

        case NDIlib_frame_type_metadata:
            std::cout <<  "NDIlib_frame_type_metadata." << std::endl;
            break;

        case NDIlib_frame_type_status_change:
            std::cout <<  "NDIlib_frame_type_status_change." << std::endl;
            break;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    stdTex->begin();
    stdTex->setIdentMatrix4fv("m_pvm");
    stdTex->setUniform1i("tex", 0);

    if (m_tex) m_tex->bind(0);

    quad->draw();


	glfwSwapBuffers(window);
}

int main(int argc, char** argv)
{
    if (!NDIlib_initialize())
    {
        printf("[ERROR] Cannot run NDI.\n");
        return -1;
    }

    // Create a finder
    NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
    if (!pNDI_find) return -1;

    // Wait until there is one source
    uint32_t no_sources = 0;
    while (!no_sources)
    {	// Wait until the sources on the nwtork have changed
        std::cout << "Looking for sources ..." << std::endl;
        NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
        p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
    }

    std::cout << "found source " << p_sources[0].p_ndi_name << " " << p_sources[0].p_url_address << std::endl;

    recv_desc.color_format = recv_col_format;

    pNDI_recv = NDIlib_recv_create_v3(&recv_desc);
    if (!pNDI_recv) return 0;

    // Connect to our sources
    NDIlib_recv_connect(pNDI_recv, p_sources + 0);

    // Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
    NDIlib_find_destroy(pNDI_find);

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

    NDIlib_recv_destroy(pNDI_recv);
    NDIlib_destroy();

    LOG << "bombich";
}
