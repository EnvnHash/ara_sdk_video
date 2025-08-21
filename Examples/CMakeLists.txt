cmake_minimum_required(VERSION 3.12)

add_subdirectory(FFmpeg)

if (ARA_USE_PORTAUDIO)
    add_subdirectory(Portaudio)
endif()

if (NOT ANDROID)
    if (NOT APPLE)
        add_subdirectory(NDI)
    endif()

    if (ARA_USE_REALSENSE)
        add_subdirectory(Realsense2)
    endif ()
endif()
