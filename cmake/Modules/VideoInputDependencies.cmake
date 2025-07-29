include(${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules/CmakeUtils.cmake)

set(THIRD_PARTY ${ARA_AV_SOURCE_DIR}/Libraries/third_party)
include_directories(${ARA_AV_SOURCE_DIR}/src)

if (ARA_USE_FFMPEG)
    if(ANDROID)
        if (${CMAKE_ANDROID_ARCH_ABI} STREQUAL "armeabi-v7a")
            append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavcodec_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavdevice_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavfilter_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavformat_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavutil_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswresample_neon.so
                    ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswscale_neon.so)
        else()
            append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavcodec.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavdevice.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavfilter.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavformat.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libavutil.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswresample.so
                ${ARA_AV_SOURCE_DIR}/third_party/ffmpeg/Android/${CMAKE_ANDROID_ARCH_ABI}/libswscale.so)
        endif()
    elseif(WIN32)
        target_link_libraries (${PROJECT_NAME}
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avcodec.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avdevice.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avfilter.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avformat.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/avutil.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/postproc.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/swresample.lib
                ${THIRD_PARTY}/ffmpeg/lib/${LIB_ARCH_PATH}/swscale.lib)
    else()
        find_package (FFMpeg REQUIRED)
        if (FFMPEG_FOUND)
            append_unique(ARA_SDK_TARGET_LINK_LIBS ${FFMPEG_LIBRARIES})
        endif ()
    endif()
endif()

#NDI
if (NOT COMPILE_SCENEGRAPH_LIB AND ARA_USE_NDI)
    if (WIN32)
        append_unique(ARA_SDK_TARGET_LINK_LIBS
            ${ARA_AV_SOURCE_DIR}/third_party/NDI/lib/${LIB_ARCH_PATH}/Processing.NDI.Lib.${LIB_ARCH_PATH}.lib
            )
    elseif(APPLE)
        #append_unique(ARA_SDK_TARGET_LINK_LIBS
         #   ${ARA_AV_SOURCE_DIR}/third_party/NDI/OsX/lib/${LIB_ARCH_PATH}/libndi.4.dylib )
    elseif(UNIX AND NOT ANDROID)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/NDI/linux/lib/x86_64-linux-gnu/libndi.so)
    elseif(ANDROID)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/NDI/Android/${CMAKE_ANDROID_ARCH_ABI}/libndi.so)
    endif()
endif()

#OpenAL
if (NOT COMPILE_SCENEGRAPH_LIB AND ARA_USE_OPENAL)
    if (WIN32)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/lib/x64/OpenAL32.lib)
    elseif(APPLE)
        find_package (OpenAL REQUIRED)
        if (OPENAL_FOUND)
            append_unique(ARA_SDK_TARGET_LINK_LIBS ${OPENAL_LIBRARY})
        endif()
    elseif(ANDROID)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/OpenAL/Android/${CMAKE_ANDROID_ARCH_ABI}/libopenal.so)
    elseif(UNIX)
        append_unique(ARA_SDK_TARGET_LINK_LIBS openal)
    endif()
endif()

#portaudio
if (ARA_USE_PORTAUDIO)
    if (WIN32)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/portaudio/lib/${LIB_ARCH_PATH}/portaudio_${LIB_ARCH_PATH}.lib)
    #elseif(APPLE)
    #    append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/portaudio/bin/OsX/libportaudio.dylib)
    elseif(ANDROID)
        append_unique(ARA_SDK_TARGET_LINK_LIBS OpenSLES ${ARA_AV_SOURCE_DIR}/third_party/portaudio/Android/${CMAKE_ANDROID_ARCH_ABI}/libportaudio.so)
    elseif(UNIX AND NOT ANDROID)
        find_package(Portaudio)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${PORTAUDIO_LIBRARIES})
    endif()
endif()

#realsense
if(ARA_USE_REALSENSE)
    if (WIN32)
        append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/intel_realsense/lib/${LIB_ARCH_PATH}/realsense2.lib)
    else()
        find_package (Realsense2 REQUIRED)
        if (REALSENSE2_FOUND)
            append_unique(ARA_SDK_TARGET_LINK_LIBS ${REALSENSE2_LIBRARIES})
        endif()
    endif()
endif()

#google libyuv
if (WIN32)
    append_unique(ARA_SDK_TARGET_LINK_LIBS ${ARA_AV_SOURCE_DIR}/third_party/libyuv/lib/${LIB_ARCH_PATH}/yuv.lib)
elseif(UNIX AND NOT ANDROID)
    append_unique(ARA_SDK_TARGET_LINK_LIBS yuv)
endif()

# opencv
if (WIN32 AND ARA_USE_OPENCV)
    find_package(OpenCV REQUIRED)
    include_directories(${OpenCV_INCLUDE_DIRS})
    append_unique(ARA_SDK_TARGET_LINK_LIBS ${OpenCV_LIBS})
    #include_directories(${ARA_AV_SOURCE_DIR}/third_party/opencv/include)
endif()
