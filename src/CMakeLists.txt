cmake_minimum_required(VERSION 3.12)

project(AudioVideo VERSION 0.1 LANGUAGES CXX)

include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/GeneralSettings.cmake)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_compile_definitions(VIDEO_EXPORT)

if(UNIX)
	set(CUDA_DIR /usr/local/cuda)
endif()

include_directories(
	${ARA_AV_SOURCE_DIR}/src
	${ARA_AV_SOURCE_DIR}/third_party/libyuv/include
	${ARA_SDK_SOURCE_DIR}/Libraries/third_party/nameof
	)

include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDepInclude.cmake)

file(GLOB GLB_SRC *.cpp)
file(GLOB AUDIOFILE_SRC AudioFile/*.cpp)

if (ARA_USE_FFMPEG)
	file(GLOB GLB_FFMPEG FFMpeg/*.cpp)
endif()
if (ARA_USE_OPENAL)
	file(GLOB GLB_OPENAL OpenAL/*.cpp)
endif()
if (ARA_USE_PORTAUDIO)
	file(GLOB GLB_PORTAUDIO Portaudio/*.cpp)
endif()
if (ARA_USE_NDI AND NOT APPLE)
	file(GLOB GLB_NDI NDI/*.cpp)
endif()
if (ANDROID AND ARA_USE_ARCORE)
	file(GLOB GLB_ARCORE ARCore/*.cpp)
endif()
if (ARA_USE_REALSENSE AND NOT ANDROID)
	file(GLOB GLB_REALSENSE IntelRealsense/*.cpp)
endif()

set(REQUIRED_THIRD_PARTY_DIRS )
if (ARA_USE_FFMPEG)
	LIST(APPEND REQUIRED_THIRD_PARTY_DIRS "ffmpeg")
endif()

if (ARA_USE_NDI)
	LIST(APPEND REQUIRED_THIRD_PARTY_DIRS "NDI")
endif()

# Main script logic
find_program(UNZIP_EXECUTABLE NAMES unzip)

if(NOT EXISTS "${ARA_AV_SOURCE_DIR}/third_party/__checkout")
	file(MAKE_DIRECTORY ${ARA_AV_SOURCE_DIR}/third_party/__checkout)
endif()

foreach(DIR IN LISTS REQUIRED_THIRD_PARTY_DIRS)
	if (NOT EXISTS "${ARA_AV_SOURCE_DIR}/third_party/__checkout/${DIR}.tar.gz")
		message(STATUS "\t downloading ${DIR}")

		file(DOWNLOAD
				ftp://ara_ftp:JcjUIUYcr74x@zeitkunst.eu/${DIR}.tar.gz
				"${ARA_AV_SOURCE_DIR}/third_party/__checkout/${DIR}.tar.gz"
				SHOW_PROGRESS
		)

		message(STATUS ${error})

		if(NOT EXISTS "${ARA_AV_SOURCE_DIR}/third_party/${DIR}")
			message(STATUS "extracting ${DIR}.tar.gz")
			execute_process(
					COMMAND tar -xvf ${ARA_AV_SOURCE_DIR}/third_party/__checkout/${DIR}.tar.gz
					WORKING_DIRECTORY ${ARA_AV_SOURCE_DIR}/third_party/
					RESULT_VARIABLE result
			)
		endif()
	endif ()

endforeach()


set(VIDEO_ALL_SRC ${GLB_SRC} ${AUDIOFILE_SRC} ${GLB_FFMPEG} ${GLB_ARCORE} ${GLB_NDI} ${GLB_PORTAUDIO} ${GLB_OPENAL} ${GLB_REALSENSE} CACHE INTERNAL "video_src_all")

if (VIDEO_ALL_SRC)
	add_library(${PROJECT_NAME} OBJECT ${VIDEO_ALL_SRC})
endif ()
