cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)

include(GeneralSettings)
include(VisualStudioSettings)

project(SineTest)

include_directories(
    ${ARA_AV_SOURCE_DIR}/src
)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(GLBaseDependencies)
include(GLBaseCopyDependencies)
include(Gtk3Include)
include(UtilitiesDependencies)
include(CMakeRC)
include(ResCompInclude)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/AraSdkVideo.cmake)

set(TARGET_LIBS ${ara_sdk_video_LIBRARIES})

list(APPEND TARGET_LIBS GLBase SceneGraph AudioVideo Utilities)

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})