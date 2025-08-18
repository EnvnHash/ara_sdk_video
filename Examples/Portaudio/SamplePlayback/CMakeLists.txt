cmake_minimum_required(VERSION 3.12)

project(SamplePlayback)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/AraSdk.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/AraSdkVideo.cmake)

include_directories(
    ${ara_sdk_INCLUDE_DIRS}
    ${ara_sdk_video_INCLUDE_DIRS}
)

target_link_libraries (${PROJECT_NAME} ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Assets/audio/stereol.wav ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Assets/audio/sine.wav ${CMAKE_CURRENT_BINARY_DIR}
)
