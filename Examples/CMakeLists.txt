cmake_minimum_required(VERSION 3.12)

project(Examples)

add_subdirectory(FFmpeg)
if (NOT APPLE)
    add_subdirectory(NDI)
endif()
add_subdirectory(Realsense2)
if (ARA_USE_PORTAUDIO)
    add_subdirectory(Portaudio)
endif()