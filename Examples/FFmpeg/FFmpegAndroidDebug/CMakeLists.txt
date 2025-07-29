cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

include(GeneralSettings)
include(VisualStudioSettings)

project(FFmpegAndroidDebug)

include_directories(
		${CMAKE_SOURCE_DIR}/Libraries/GLBase/src
		${CMAKE_SOURCE_DIR}/Libraries/VideoInput/src
		${CMAKE_SOURCE_DIR}/Libraries/Utilities/src
		${ARA_SDK_SOURCE_DIR}/Libraries/third_party/nameof
)

set(SOURCES main.cpp)

#gen_android_proj(${PROJECT_NAME})
