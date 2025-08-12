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
#include "Utils/Typo/TypoGlyphMap.h"

using namespace std;
using namespace ara;
using namespace ara::av;

bool			    printFps = false;
bool			    inited = false;

GLFWwindow*		    window = nullptr;
GLBase              glbase;
Shaders*            stdTex;
Shaders*            stdCol;
unique_ptr<Quad>    quad;
FFMpegPlayer        player;
unique_ptr<TypoGlyphMap> typo;

int				    winWidth = 1280;
int				    winHeight = 720;
double 			    actTime = 0.0;
double 			    timeFmod = 0.0;
double              dt = 0.0;
glm::vec4           tCol(1.f, 0.4f, 0.4f, 1.f);
float               barHeight=0.01f;
float               barWidth=0.2f;
float               infoBlockLeft=0.32f;
float               lableWidth=0.4f;
float               infoBlockTop=-0.8f;
float               audioInfoBlockLeft=0.58f;

static void output_error(int error, const char* msg) {
	fprintf(stderr, "Error: %s\n", msg);
}

static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
	    player.stop();
        glfwSetWindowShouldClose(win, GLFW_TRUE);
	}
}

void init() {
    glbase.init(false);
    glfwMakeContextCurrent(window);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{0.f, 0.f, 0.f, 1.f}, .flipHori = true });

    player.openFile({
        .glbase = &glbase,
        .filePath = "trailer_1080p.mov",
        .destWidth = winWidth,
        .destHeight = winHeight
    });

    //player.openFile(&glbase, "trailer_1080p_nosound.mov", winWidth, winHeight);
    player.start(glfwGetTime());

    string vert = STRINGIFY(layout(location = 0) in vec4 position;          \n
        uniform mat4 m_pvm;                                                 \n
        uniform vec2 size;                                                  \n
        uniform vec2 pos;                                                   \n
        void main() {                                                       \n
            gl_Position = m_pvm * vec4(position.xy * size + pos, 0.0, 1.0); \n
    });
    vert = ShaderCollector::getShaderHeader() + vert;

    std::string frag = STRINGIFY(layout(location = 0) out vec4 glFragColor; \n
        uniform vec4 color;                                                 \n
        void main() {                                                       \n
            glFragColor = color;                                            \n
    });
    frag = ShaderCollector::getShaderHeader() + frag;
    stdCol = ShaderCollector().add("buffStat", vert, frag);

    typo = make_unique<TypoGlyphMap>(winWidth, winHeight);
    typo->loadFont((filesystem::path("resdata") / "Fonts" / "open-sans" / "OpenSans-Light.ttf").string().c_str(), &glbase.shaderCollector());
}

void drawBufferStatus() {
    std::string bFilledLabl = "V-Buffers filled " + std::to_string(player.getNrBufferedFrames()) + "/" + std::to_string(player.getVideoFrameBufferSize());
    typo->print(infoBlockLeft, infoBlockTop, bFilledLabl, 18, &tCol[0]);

    stdCol->begin();
    float bufFilled = (float) player.getNrBufferedFrames() / (float)(player.getVideoFrameBufferSize());
    stdCol->setIdentMatrix4fv("m_pvm");
    stdCol->setUniform4f("color", 0.f, 1.f, 0.f, 1.f);
    stdCol->setUniform2f("size", bufFilled * barWidth, barHeight);
    stdCol->setUniform2f("pos",  infoBlockLeft + lableWidth - (1.f - bufFilled) * barWidth, infoBlockTop + barHeight *1.5);

    quad->draw();
}

void drawAudioWriteBuffer() {
    std::string decLabl = "A-Write Buffer: "+std::to_string(player.getAudioWriteBufIdx());
    typo->print(audioInfoBlockLeft, infoBlockTop - barHeight*4, decLabl, 18, &tCol[0]);

    stdCol->begin();
    stdCol->setIdentMatrix4fv("m_pvm");
    stdCol->setUniform4f("color", 1.f, 0.f, 0.f, 1.f);
    stdCol->setUniform2f("size", barHeight, barHeight);
    stdCol->setUniform2f("pos",
                         audioInfoBlockLeft + lableWidth + (float) player.getWriteFramePtr() / (float)player.getVideoFrameBufferSize() * barWidth * 2.f - barWidth,
                         infoBlockTop - barHeight * 2.5);

    quad->draw();
}

void drawAudioReadBuffer() {
    std::string uplLabl = "A-Read Buffer: "+std::to_string(player.getAudioReadBufIdx());
    typo->print(audioInfoBlockLeft, infoBlockTop - barHeight*8, uplLabl, 18, &tCol[0]);

    stdCol->begin();
    stdCol->setIdentMatrix4fv("m_pvm");
    stdCol->setUniform4f("color", 0.f, 0.f, 1.f, 1.f);
    stdCol->setUniform2f("size", barHeight, barHeight);
    stdCol->setUniform2f("pos",
                         infoBlockLeft + lableWidth + (float) player.getReadFramePtr() / (float)player.getVideoFrameBufferSize() * barWidth * 2.f - barWidth,
                         infoBlockTop - barHeight*6.5);

    quad->draw();
}

void drawDecodeBufferNr() {
    std::string decLabl = "V-Decode Buffer: "+std::to_string(player.getWriteFramePtr());
    typo->print(infoBlockLeft, infoBlockTop - barHeight*4, decLabl, 18, &tCol[0]);

    stdCol->begin();
    stdCol->setIdentMatrix4fv("m_pvm");
    stdCol->setUniform4f("color", 1.f, 0.f, 0.f, 1.f);
    stdCol->setUniform2f("size", barHeight, barHeight);
    stdCol->setUniform2f("pos",
                         infoBlockLeft + lableWidth + (float) player.getWriteFramePtr() / (float)player.getVideoFrameBufferSize() * barWidth * 2.f - barWidth,
                         infoBlockTop - barHeight*2.5);

    quad->draw();
}

void drawFrameToUploadNr() {
    std::string uplLabl = "V-Upload Buffer: "+std::to_string(player.getReadFramePtr());
    typo->print(infoBlockLeft, infoBlockTop - barHeight*8, uplLabl, 18, &tCol[0]);

    stdCol->begin();
    stdCol->setIdentMatrix4fv("m_pvm");
    stdCol->setUniform4f("color", 0.f, 0.f, 1.f, 1.f);
    stdCol->setUniform2f("size", barHeight, barHeight);
    stdCol->setUniform2f("pos",
                         infoBlockLeft + lableWidth + (float) player.getReadFramePtr() / (float)player.getVideoFrameBufferSize() * barWidth * 2.f - barWidth,
    infoBlockTop - barHeight*6.5);

    quad->draw();
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

    if (!inited){
        init();
        glfwSwapBuffers(window);
        inited = true;
    }

    glEnable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    player.loadFrameToTexture(glfwGetTime());
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    player.shaderBegin(); // draw with conversion yuv -> rgb on gpu
    quad->draw();

    drawBufferStatus();
    drawDecodeBufferNr();
    drawFrameToUploadNr();

    drawAudioWriteBuffer();
    drawAudioReadBuffer();

    glfwSwapBuffers(window);
}

int main(int argc, char** argv) {
    glfwSetErrorCallback(output_error);

	if (!glfwInit())
		LOGE << "Failed to initialize GLFW";

#ifdef __APPLE__
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GL_TRUE); // if GL_FALSE pixel sizing is 1:1 if GL_TRUE the required size will be different from the resulting window size
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

 	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
	glfwWindowHint(GLFW_DECORATED, GL_TRUE);

	window = glfwCreateWindow(winWidth, winHeight, "FFmpeg Test", nullptr, nullptr);
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

    initGLEW();
    glViewport(0, 0, winWidth, winHeight);

    while (!glfwWindowShouldClose(window)) {
		display();
        glfwPollEvents();
    }

    LOG << "bombich";
}
