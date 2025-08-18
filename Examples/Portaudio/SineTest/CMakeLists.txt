cmake_minimum_required(VERSION 3.12)

project(SineTest)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/AraSdk.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/AraSdkVideo.cmake)

include_directories(
        ${ara_sdk_INCLUDE_DIRS}
        ${ara_sdk_video_INCLUDE_DIRS}
)

target_link_libraries (${PROJECT_NAME} ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)