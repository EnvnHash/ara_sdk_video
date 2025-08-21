cmake_minimum_required(VERSION 3.12)

project(SineTest)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries (${PROJECT_NAME} ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)