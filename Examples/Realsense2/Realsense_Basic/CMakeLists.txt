cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GeneralSettings)
include(VisualStudioSettings)

project(Realsense_Basic)

find_package (Realsense2 REQUIRED)
if (REALSENSE2_FOUND)
    include_directories(${REALSENSE2_INCLUDE_DIR})
endif()

find_package (OpenGL REQUIRED)
if (NOT WIN32)
	find_package (GLFW REQUIRED)
endif()

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(GLBaseDependencies)
include(GLBaseCopyDependencies)
include(Gtk3Include)
include(UtilitiesDependencies)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDependencies.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputCopyDependencies.cmake)

set(TARGET_LIBS GLBase GLSceneGraph VideoInput Utilities)

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})

