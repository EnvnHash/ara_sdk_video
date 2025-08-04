include_directories(${ARA_AV_SOURCE_DIR}/src)
set(THIRD_PARTY ${ARA_AV_SOURCE_DIR}/third_party)

if(UNIX)
    set(CUDA_DIR /usr/local/cuda)
    list(APPEND CMAKE_PREFIX_PATH ${CUDA_DIR})
endif()

include_directories(${THIRD_PARTY})

# FFMpeg
if(ARA_USE_FFMPEG)
    if (WIN32)
        include_directories(${THIRD_PARTY}/ffmpeg/include)
    else()
        if (ANDROID)
            include_directories(${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/include)
        else()
            find_package (FFMpeg REQUIRED)
            if (FFMPEG_FOUND)
                include_directories(${FFMPEG_INCLUDE_DIRS})
            endif (FFMPEG_FOUND)
        endif()
    endif()
endif()

#NDI
if(ANDROID)
    include_directories(${ARA_AV_SOURCE_DIR}/third_party/NDI/Android/Include)
else()
    message(STATUS ${ARA_AV_SOURCE_DIR}/third_party/NDI/include)
    include_directories(${ARA_AV_SOURCE_DIR}/third_party/NDI/include)
endif()

#portaudio
if (ARA_USE_PORTAUDIO)
    if (WIN32)
        include_directories(${ARA_AV_SOURCE_DIR}/third_party/portaudio/include)
    elseif(ANDROID)
        include_directories(${ARA_AV_SOURCE_DIR}/third_party/portaudio/Android/include)
    endif()
endif()

#realsense
if(ARA_USE_REALSENSE)
    if (WIN32)
        include_directories(${ARA_AV_SOURCE_DIR}/third_party/intel_realsense/include)
    elseif(NOT ANDROID)
        find_package (Realsense2 REQUIRED)
        if (REALSENSE2_FOUND)
            include_directories(${REALSENSE2_INCLUDE_DIR})
        endif()
    endif()
endif()

#google libyuv
include_directories(${ARA_AV_SOURCE_DIR}/third_party/libyuv/include)

#libobs
if (ARA_USE_OBS AND WIN32)
    include_directories(${ARA_AV_SOURCE_DIR}/third_party/obs/include)
endif()

#openal
if (ARA_USE_OPENAL)
    if (WIN32)
        include_directories(${ARA_AV_SOURCE_DIR}/third_party/OpenAL/include)
    elseif(ANDROID)
        include_directories(${ARA_AV_SOURCE_DIR}/third_party/OpenAL/Android/include)
    elseif(APPLE)
        find_package (OpenAL REQUIRED)
        if (OPENAL_FOUND)
            include_directories(${OPENAL_INCLUDE_DIR})
        endif()
    else()
        find_package (OpenAL REQUIRED)
        if (OPENAL_FOUND)
            include_directories(${OPENAL_INCLUDE_DIR})
        endif()
    endif()
endif()

#opencv
if (ARA_USE_OPENCV)
    find_package(OpenCV REQUIRED PATHS "..")
    if (OPENCV_FOUND)
        include_directories(${OpenCV_INCLUDE_DIRS})
    else()
        message(ERROR "OpenCV not found")
    endif()
endif()

#ARCORE
if (ANDROID AND ARA_USE_ARCORE)
    message(STATUS "INCLUDE ARCORE")
    include_directories(${ARA_AV_SOURCE_DIR}/third_party/ARCore/include)
endif()