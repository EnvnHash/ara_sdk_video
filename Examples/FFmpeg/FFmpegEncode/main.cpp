/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */


#include <FFMpeg/FFMpegEncode.h>
#include <WindowManagement/GLFWWindow.h>

#include <GeoPrimitives/Quad.h>

using namespace std;
using namespace ara;
using namespace ara::av;

bool			    printFps = true;
bool			    inited = false;

GLFWwindow*		    window = nullptr;
ShaderCollector     shCol;
Shaders*            testPic=nullptr;
unique_ptr<Quad>    quad;
FFMpegEncode        encoder;
int				    winWidth = 1280;
int				    winHeight = 720;
int				    animCntr = 0;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;
double              medDt = 0.0;
int                 downIt=0;

static void output_error(int error, const char* msg) {
	fprintf(stderr, "Error: %s\n", msg);
}

static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(win, GLFW_TRUE);
	}
}

void init() {
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{1.f, 0.f, 0.f, 1.f}, .flipHori = true });

    std::string vert = STRINGIFY( layout(location = 0) in vec4 position;
          layout(location = 2) in vec2 texCoord;
          uniform mat4 m_pvm;
          out vec2 tex_coord;
          void main() {
              tex_coord = texCoord;
              gl_Position = m_pvm * position;
          });
    vert = shCol.getShaderHeader() + "// test pic shader, vert\n" + vert;

    std::string frag = STRINGIFY(in vec2 tex_coord;
        uniform float time;
         layout(location = 0) out vec4 color;
         void main() {
             float w=0.1;
             float pr = mod(tex_coord.x + cos(tex_coord.y * 6.28 + time*0.1)*0.5 + time*0.04, 1.0);
             float phas = mod(pr*4.0, 1.0);
             color = vec4(smoothstep(w, 0.0, phas) + smoothstep(1.0-w, 1.0, phas));
             pr = pr -w;
             color.r *= (pr < 0.25 || pr > 0.75) ? 1.0 : 0.0;
             color.g *= (pr > 0.25 && pr < 0.5) || pr > 0.75 || pr < 0.0 ? 1.0 : 0.0;
             color.b *= (pr > 0.5 && pr < 0.75) || pr > 0.75 || pr < 0.0 ? 1.0 : 0.0;
         });

    frag = shCol.getShaderHeader()  + "// test pic shader, frag\n" + frag;

    testPic = shCol.add("test_pic", vert, frag);

    int bitRate = 1048576 *3; // 1 MBit = 1.048.576. ab 4 Mbit probleme bei rtmp streams auf windows
    encoder.setVideoBitRate(bitRate);
    //bool ret = encoder.init("rtmp://192.168.1.103/live/video_test", winWidth, winHeight, 30, AV_PIX_FMT_BGRA, true);
    //bool ret = encoder.init("rtmp://unstumm.com/live/realsense", winWidth, winHeight, 30, AV_PIX_FMT_BGRA, true);
    bool ret = encoder.init("video_test.mp4", winWidth, winHeight, 30, AV_PIX_FMT_BGRA, true);
    if (ret) {
        encoder.record();
    } else {
        LOGE << "Could not init encoder";
    }
}

static void display() {
    // assumes 60hz loop
    if (!inited) {
        init();
        inited = true;
    }

    if ((downIt % 2) == 0){
        encoder.downloadGlFbToVideoFrame(30.0);

        dt = glfwGetTime() - actTime;
        actTime = glfwGetTime();

        // check Framerate every 2 seconds
        if (printFps) {
            double newTimeFmod = std::fmod(glfwGetTime(), 2.0);
            if (newTimeFmod < timeFmod) {
                if (!medDt) {
                    medDt = dt;
                }
                medDt = (medDt * 20.0 + dt) / 21.0;
               LOG <<  "raw dt: " << dt << " dt: " << medDt << " fps: " <<  (1.0 / medDt);
            }
            timeFmod = newTimeFmod;
        }
    }

    ++downIt;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    testPic->begin();
    testPic->setUniform1f("time", static_cast<float>(animCntr++) * 0.1f);
    testPic->setIdentMatrix4fv("m_pvm");

    quad->draw();

	glfwSwapBuffers(window);

    if (animCntr % 2 == 0) {
        encoder.downloadGlFbToVideoFrame(30.0);
    }
}

int main(int argc, char** argv) {
    glfwSetErrorCallback(output_error);

	if (!glfwInit()) {
        LOGE << "Failed to initialize GLFW";
    }

 	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
	glfwWindowHint(GLFW_DECORATED, GL_TRUE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GL_TRUE); // if GL_FALSE pixel sizing is 1:1 if GL_TRUE the required size will be different from the resulting window size
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

	window = glfwCreateWindow(winWidth, winHeight, "FFMpeg Encode Test", nullptr, nullptr);
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

    encoder.stop();
    encoder.freeGlResources();

    glfwDestroyWindow(window);
    glfwTerminate();

    exit(0);
}
