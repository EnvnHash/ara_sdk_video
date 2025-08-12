/*
 * main.cpp
 *
 *  Created on: Jun 20, 2019
 *      Author: sven
 */

#include "FFMpeg/FFMpegPlayer.h"
#include "FFMpeg/FFMpegEncode.h"
#include <WindowManagement/GLFWWindow.h>
#include <GeoPrimitives/Quad.h>
#include <GLBase.h>
#include <StopWatch.h>

using namespace std;
using namespace ara;
using namespace ara::av;

FFMpegPlayer        decoder;
FFMpegEncode        encoder;
GLBase              glbase;
StopWatch           fpsWatch;
StopWatch           decodeWatch;
GLFWwindow*		    window = nullptr;
ShaderCollector     shCol;
unique_ptr<Quad>    quad;

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
    quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{0.f, 0.f, 0.f, 1.f}, .flipHori = true });
}

static void display() {
    fpsWatch.setStart();
    fpsWatch.setEnd();
    //fpsWatch.print("decode time: ");

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    decoder.loadFrameToTexture(0.0, true);
    decoder.shaderBegin(); // draw with conversion yuv -> rgb on gpu
    quad->draw();
    glfwSwapBuffers(window);

    encoder.downloadGlFbToVideoFrame(static_cast<int32_t>(decoder.getFps(ffmpeg::streamType::video)), nullptr, true);

    if (decoder.getPaudio().isRunning()){
        // fake audio consume
        while (decoder.getPaudio().getCycleBuffer().getFillAmt() > 0) {
            decoder.getPaudio().getCycleBuffer().consumeCountUp();
        }
    }
}

void initGlfw() {
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

    window = glfwCreateWindow(100, 100, "FFMpeg Re-encode Test", nullptr, nullptr);
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
    glfwSwapInterval(0); // run as fast as possible

    LOG << "Vendor:   " << glGetString(GL_VENDOR);
    LOG << "Renderer: " << glGetString(GL_RENDERER);
    LOG << "Version:  " << glGetString(GL_VERSION);
    LOG << "GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION);
}

int main(int, char**) {
    bool run = true;
    decodeWatch.setStart();

    initGlfw();
    initGLEW();
    glbase.init(false);
    glfwMakeContextCurrent(window);

    decoder.openFile({
        .glbase = &glbase,
        .filePath = "trailer_1080p.mov",
        .destWidth = 1920,
        .destHeight = 1080,
        .loop = false,
        .endCb = [&] { run = false; }
    });
    decoder.start(glfwGetTime());

    glfwSetWindowSize(window, decoder.getPar().destWidth, decoder.getPar().destHeight);
    glViewport(0, 0, decoder.getPar().destWidth, decoder.getPar().destHeight);

    encoder.init({
        .filePath = "trailer_1080p_transc.mov",
        .pixelFormat = AV_PIX_FMT_BGRA,
        .width = decoder.getPar().destWidth,
        .height = decoder.getPar().destHeight,
        .fps = static_cast<int32_t>(decoder.getFps(ffmpeg::streamType::video)),
        .videoBitRate = 1048576 *7,
        .useHwAccel = true
    });
    encoder.record();

    init();

    while (run && !glfwWindowShouldClose(window)) {
        display();
        glfwPollEvents();
    }

    encoder.stop();
    encoder.freeGlResources();

    glfwDestroyWindow(window);
    glfwTerminate();

    decodeWatch.setEnd();
    LOG << "bombich! decode took: " << decodeWatch.getDt() << " ms";

    return 0;
}
