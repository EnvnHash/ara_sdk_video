cmake_minimum_required(VERSION 3.12)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

include(GeneralSettings)
include(VisualStudioSettings)

project(NDISimpleReceiver)

include_directories(
	${CMAKE_SOURCE_DIR}/Libraries/GLBase/src
	${CMAKE_SOURCE_DIR}/Libraries/VideoInput/src
	${CMAKE_SOURCE_DIR}/Libraries/Utilities/src
	${ARA_SDK_SOURCE_DIR}/Libraries/third_party/nameof
)

#link_directories(${PROJECT_DESIGNER_DLL_DIR})

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(GLBaseDependencies)
include(GLBaseCopyDependencies)
include(Gtk3Include)
include(PugiXMLInclude)
include(UtilitiesDependencies)
include(CMakeRC)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDependencies.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputCopyDependencies.cmake)

set(TARGET_LIBS GLBase GLSceneGraph VideoInput Utilities)
if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

if (WIN32)
	target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS} Dwmapi.lib)
else ()
	target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})
endif()

INSTALL(TARGETS ${PROJECT_NAME}
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)