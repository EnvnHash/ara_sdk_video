cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)

include(GeneralSettings)
include(VisualStudioSettings)

project(FFmpegReencode)

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
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDepInclude.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDependencies.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputCopyDependencies.cmake)

set(TARGET_LIBS ${ARA_SDK_TARGET_LINK_LIBS})

list(APPEND TARGET_LIBS GLBase SceneGraph AudioVideo Utilities)
if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

if (WIN32)
	list(APPEND TARGET_LIBS Dwmapi.lib)
endif()

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Assets/videos/trailer_1080p.mov ${CMAKE_CURRENT_BINARY_DIR}
)

INSTALL(TARGETS ${PROJECT_NAME}
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)