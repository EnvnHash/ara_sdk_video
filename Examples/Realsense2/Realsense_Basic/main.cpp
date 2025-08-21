// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2019 Intel Corporation. All Rights Reserved.

#include <librealsense2/rs.hpp>
#include <GLBase.h>
#include "example-utils.hpp"

using namespace std;
using namespace ara;

GLFWWindow gwin;           // create an instance, this will do nothing
GLBase m_glbase;
ShaderCollector shCol;  // create a ShaderCollector
Shaders* texShader;
unique_ptr<Quad> quad;
bool run = true;

enum class direction
{
    to_depth,
    to_color
};

bool staticDrawFunc(double, double, int)
{
    Texture tex(&m_glbase);
    texShader = shCol.getStdTex(); // get a simple standard color shader
    quad = make_unique<Quad>(QuadInitParams{ .color = glm::vec4{0.f, 0.f, 0.f, 1.f}, .flipHori = true });

    std::string serial;
    if (!device_with_streams({ RS2_STREAM_COLOR,RS2_STREAM_DEPTH }, serial))
        return EXIT_SUCCESS;

    rs2::colorizer c;                     // Helper to colorize depth images

    // Create a pipeline to easily configure and start the camera
    rs2::pipeline pipe;
    rs2::config cfg;
    if (!serial.empty()) cfg.enable_device(serial);
    cfg.enable_stream(RS2_STREAM_DEPTH);
    cfg.enable_stream(RS2_STREAM_COLOR);
    pipe.start(cfg);

    // Define two align objects. One will be used to align to depth viewport and the other to color.
    // Creating align object is an expensive operation that should not be performed in the main loop
    rs2::align align_to_depth(RS2_STREAM_DEPTH);
    rs2::align align_to_color(RS2_STREAM_COLOR);

    direction   dir = direction::to_color;  // Alignment direction

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    texShader->begin();
    texShader->setIdentMatrix4fv("m_pvm");
    texShader->setUniform1i("tex", 0);

    while (run) // Application still alive?
    {
        // Using the align object, we block the application until a frameset is available
        rs2::frameset frameset = pipe.wait_for_frames();

        if (dir == direction::to_depth)
            frameset = align_to_depth.process(frameset);
        else
            frameset = align_to_color.process(frameset);

        auto color = frameset.get_color_frame();

        if (!tex.isAllocated())
            tex.allocate2D(color.get_width(), color.get_height(), GL_RGB8, GL_RGB, GL_TEXTURE_2D, GL_UNSIGNED_BYTE);

        tex.upload((void*)color.get_data());

        tex.bind(0);
        quad->draw();

        gwin.swap();
        glfwPollEvents();
    }

    gwin.close();
    return false;
}

int main(int argc, char * argv[]) try
{
    // direct window creation
    gwin.init({
        .shift = { 10, 10 },  //  offset relative to OS screen canvas
        .size = { 1280, 7200 }  // set the windows size
    });    // now pass the arguments and create the window
    ara::initGLEW();

    // start a draw loop
    gwin.runLoop(staticDrawFunc);

    return 0;
}
catch (const rs2::error & e)
{
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception & e)
{
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
