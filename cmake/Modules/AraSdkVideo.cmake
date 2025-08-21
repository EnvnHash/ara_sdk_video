if (NOT DEFINED ARA_AV_SOURCE_DIR)
    set(ARA_AV_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
endif ()

set(CMAKE_MODULE_PATH ${ARA_AV_SOURCE_DIR}/cmake/Modules)

include(AraSdkVideoConfigure)
include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/AraSdkMacros.cmake)

set(ara_sdk_video_INCLUDE_DIRS)
set(ara_sdk_video_LIBRARIES)

append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/src)
set(THIRD_PARTY ${ARA_AV_SOURCE_DIR}/third_party)
append_unique(ara_sdk_video_INCLUDE_DIRS ${THIRD_PARTY})

set(THIRD_PARTY_LIB_DIR ${ARA_AV_SOURCE_DIR}/Libraries/third_party)

if(UNIX)
    set(CUDA_DIR /usr/local/cuda)
    list(APPEND CMAKE_PREFIX_PATH ${CUDA_DIR})
endif()

# translate cmake switches to preprocessor flags - nothing to do configure manually here
if (ARA_USE_REALSENSE)
    add_compile_definitions(ARA_USE_REALSENSE)
endif()

# FFMpeg
if (ARA_USE_FFMPEG)
    add_compile_definitions(ARA_USE_FFMPEG)
    if (WIN32)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${THIRD_PARTY}/ffmpeg/include)
    else()
        find_package(GLEW REQUIRED)
        if (NOT ARA_USE_GLES31)
            find_package(OpenGL REQUIRED)
        endif()
        if (ANDROID)
            append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/include)
        else()
            find_package(FFMpeg REQUIRED)
            if (FFMPEG_FOUND)
                append_unique(ara_sdk_video_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
            endif()
        endif()
    endif()

    if(ANDROID)
        if (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "armeabi-v7a")
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavcodec_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavdevice_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavfilter_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavformat_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavutil_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswresample_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswscale_neon.so)
        else()
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavcodec.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavdevice.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavfilter.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavformat.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavutil.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswresample.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswscale.so)
        endif()
    elseif(WIN32)
        append_unique(ara_sdk_video_LIBRARIES
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avcodec.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avdevice.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avfilter.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avformat.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avutil.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/postproc.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/swresample.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/swscale.lib)
    else()
        find_package(FFMpeg REQUIRED)
        if (FFMPEG_FOUND)
            append_unique(ara_sdk_video_LIBRARIES ${FFMPEG_LIBRARIES})
        endif ()
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_BINARIES
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/avcodec-59.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/avdevice-59.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/avfilter-8.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/avformat-59.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/avutil-57.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/postproc-56.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/swresample-4.dll
            ${THIRD_PARTY_LIB_DIR}/ffmpeg/bin/${LIB_ARCH_PATH}/swscale-6.dll)
    endif()
endif()

# OpenAL
if (ARA_USE_OPENAL)
    add_compile_definitions(ARA_USE_OPENAL)

    if (WIN32)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/include)
    elseif(ANDROID)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/Android/include)
    elseif(APPLE)
        find_package(OpenAL REQUIRED)
        if (OPENAL_FOUND)
                append_unique(ara_sdk_video_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
        endif()
    else()
        find_package(OpenAL REQUIRED)
        if (OPENAL_FOUND)
            append_unique(ara_sdk_video_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
        endif()
    endif()

    if (NOT COMPILE_SCENEGRAPH_LIB)
        if (WIN32)
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/lib/x64/OpenAL32.lib)
        elseif(APPLE)
            find_package(OpenAL REQUIRED)
            if (OPENAL_FOUND)
                append_unique(ara_sdk_video_LIBRARIES ${OPENAL_LIBRARY})
            endif()
        elseif(ANDROID)
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/Android/${CMAKE_ANDROID_ARCH_ABI}/libopenal.so)
        elseif(UNIX)
            append_unique(ara_sdk_video_LIBRARIES openal)
        endif()
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_BINARIES ${THIRD_PARTY_LIB_DIR}/OpenAL/bin/${LIB_ARCH_PATH}/OpenAL32.dll)
    endif()
endif()

# ARCORE
if (ANDROID AND ARA_USE_ARCORE)
    add_compile_definitions(ARA_USE_ARCORE)
    append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/ARCore/include)
endif()

# Portaudio
if (ARA_USE_PORTAUDIO)
    add_compile_definitions(ARA_USE_PORTAUDIO)

    if (WIN32)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/portaudio/include)
    elseif(ANDROID)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/portaudio/Android/include)
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/portaudio/lib/${LIB_ARCH_PATH}/portaudio_${LIB_ARCH_PATH}.lib)
    elseif(ANDROID)
        append_unique(ara_sdk_video_LIBRARIES OpenSLES ${ARA_AV_SOURCE_DIR}/third_party/portaudio/Android/${CMAKE_ANDROID_ARCH_ABI}/libportaudio.so)
    elseif(UNIX AND NOT ANDROID)
        find_package(Portaudio)
        append_unique(ara_sdk_video_LIBRARIES ${PORTAUDIO_LIBRARIES})
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_BINARIES ${THIRD_PARTY_LIB_DIR}/portaudio/bin/${LIB_ARCH_PATH}/portaudio_${LIB_ARCH_PATH}.dll)
    endif()
endif()

# NDI
if (ARA_USE_NDI)
    add_compile_definitions(ARA_USE_NDI)
    if(ANDROID)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/NDI/Android/Include)
    else()
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/NDI/include)
    endif()

    if (NOT COMPILE_SCENEGRAPH_LIB)
        if (WIN32)
            append_unique(ara_sdk_video_LIBRARIES
                    ${ARA_AV_SOURCE_DIR}/third_party/NDI/lib/${LIB_ARCH_PATH}/Processing.NDI.Lib.${LIB_ARCH_PATH}.lib
            )
        elseif(APPLE)
            #append_unique(ara_sdk_video_LIBRARIES
            #   ${ARA_AV_SOURCE_DIR}/third_party/NDI/OsX/lib/${LIB_ARCH_PATH}/libndi.4.dylib )
        elseif(UNIX AND NOT ANDROID)
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/NDI/linux/lib/x86_64-linux-gnu/libndi.so)
        elseif(ANDROID)
            append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/NDI/Android/${CMAKE_ANDROID_ARCH_ABI}/libndi.so)
        endif()
    endif()
    if (WIN32)
        append_unique(ara_sdk_video_BINARIES ${THIRD_PARTY_LIB_DIR}/NDI/Bin/${LIB_ARCH_PATH}/Processing.NDI.Lib.${LIB_ARCH_PATH}.dll)
    endif()
endif()

# Realsense
if(ARA_USE_REALSENSE)
    if (WIN32)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/intel_realsense/include)
    elseif(NOT ANDROID)
        find_package(Realsense2 REQUIRED)
        if (REALSENSE2_FOUND)
            append_unique(ara_sdk_video_INCLUDE_DIRS ${REALSENSE2_INCLUDE_DIR})
        endif()
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/intel_realsense/lib/${LIB_ARCH_PATH}/realsense2.lib)
    else()
        find_package(Realsense2 REQUIRED)
        if (REALSENSE2_FOUND)
            append_unique(ara_sdk_video_LIBRARIES ${REALSENSE2_LIBRARIES})
        endif()
    endif()

    if (WIN32)
        append_unique(ara_sdk_video_BINARIES ${THIRD_PARTY_LIB_DIR}/intel_realsense/bin/${LIB_ARCH_PATH}/realsense2.dll)
    endif()
endif()

#google libyuv
append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_AV_SOURCE_DIR}/third_party/libyuv/include)
if (WIN32)
    append_unique(ara_sdk_video_LIBRARIES ${ARA_AV_SOURCE_DIR}/third_party/libyuv/lib/${LIB_ARCH_PATH}/yuv.lib)
    append_unique(ara_sdk_video_BINARIES ${THIRD_PARTY_LIB_DIR}/libyuv/bin/${LIB_ARCH_PATH}/libyuv.dll)
elseif(UNIX AND NOT ANDROID)
    append_unique(ara_sdk_video_LIBRARIES yuv)
endif()

#opencv
if (ARA_USE_OPENCV)
    find_package(OpenCV REQUIRED PATHS "..")
    if (OPENCV_FOUND)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
    else()
        message(ERROR "OpenCV not found")
    endif()

    if (WIN32)
        find_package(OpenCV REQUIRED)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${ARA_SDK_SOURCE_DIR}/Libraries/third_party/opencv/include)
        append_unique(ara_sdk_video_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
        append_unique(ara_sdk_video_LIBRARIES ${OpenCV_LIBS})
    else ()
        find_package(OpenCV REQUIRED PATHS "..")
        if (OPENCV_FOUND)
            append_unique(ara_sdk_video_BINARIES C:\\Program\ Files\\OpenCV\\${LIB_ARCH_PATH}\\vc15\\bin\\opencv_world454.dll)
            append_unique(ara_sdk_video_BINARIES C:\\Program\ Files\\OpenCV\\${LIB_ARCH_PATH}\\vc15\\bin\\opencv_world454d.dll)
        else()
            message(ERROR "OpenCV not found")
        endif()
    endif()
endif()