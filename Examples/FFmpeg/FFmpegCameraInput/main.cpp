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

using namespace std;
using namespace ara;
using namespace ara::av;

bool			    printFps = false;
bool			    inited = false;

GLFWwindow*		    window = nullptr;
GLBase              glbase;
Shaders*            stdTex;
unique_ptr<Quad>    quad;
FFMpegDecode        decoder;
int				    winWidth = 1280;
int				    winHeight = 800;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;

static void output_error(int error, const char* msg) {
	fprintf(stderr, "Error: %s\n", msg);
}

static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
	    decoder.stop();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

void init() {
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glbase.init(false);
    glfwMakeContextCurrent(window);
	quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{0.f, 0.f, 0.f, 1.f}, .flipHori = true });

#ifdef _WIN32
    decoder.OpenCamera(&glbase, "Logi C270 HD WebCam", winWidth, winHeight, false);
#elif __linux__
    decoder.openCamera(&glbase, "/dev/video0", winWidth, winHeight, false);
#endif
    decoder.start(glfwGetTime());
}

static void display() {
	dt = glfwGetTime() - actTime;

	// check Framerate every 2 seconds
	if (printFps) {
		double newTimeFmod = std::fmod(glfwGetTime(), 2.0);
		if (newTimeFmod < timeFmod) {
			printf("dt: %f fps: %f \n", dt, 1.0 / dt);
		}
		actTime = glfwGetTime();
		timeFmod = newTimeFmod;
	}

    if (!inited) {
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

int main(int argc, char** argv) {
    glfwSetErrorCallback(output_error);

	if (!glfwInit()) {
		LOGE << "Failed to initialize GLFW";
	}

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

    ara::initGLEW();
    glViewport(0, 0, winWidth, winHeight);

    while (!glfwWindowShouldClose(window)) {
		display();
        glfwPollEvents();
    }

    LOG << "bombich";
}
