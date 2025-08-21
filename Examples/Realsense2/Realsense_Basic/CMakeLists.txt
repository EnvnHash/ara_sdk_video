cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_AV_SOURCE_DIR}/cmake/Modules)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

project(Realsense_Basic)

find_package(Realsense2 REQUIRED)

include_directories(
    ${ara_sdk_INCLUDE_DIRS}
    ${ara_sdk_video_INCLUDE_DIRS}
)

if (REALSENSE2_FOUND)
    include_directories(${REALSENSE2_INCLUDE_DIR})
endif()

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)

