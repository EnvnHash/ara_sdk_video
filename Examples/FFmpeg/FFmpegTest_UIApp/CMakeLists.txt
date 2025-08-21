cmake_minimum_required(VERSION 3.12)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

project(FFmpegTest_UIApp)

file(GLOB UNV_SRC *.cpp)
set(SOURCES ${UNV_SRC})

if (ANDROID)
	add_library(${PROJECT_NAME} STATIC ${SOURCES})
else()
	if(${CMAKE_BUILD_TYPE} MATCHES Debug)
		add_executable(${PROJECT_NAME} ${SOURCES} ${WIN_INSTALLER_RES})
	else()
		if(WIN32)
			add_executable(${PROJECT_NAME} ${SOURCES} ${WIN_INSTALLER_RES})
			#add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${WIN_INSTALLER_RES})
		elseif(APPLE)
			add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})
			set_target_properties(${PROJECT_NAME} PROPERTIES
				BUNDLE True
				MACOSX_BUNDLE_GUI_IDENTIFIER com.unstumm.${PROJECT_NAME}
				MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
				MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
				MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
				MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Libraries/cmake/customtemplate.plist.in
				)
		else()
			add_executable(${PROJECT_NAME} ${SOURCES})
		endif()
	endif()
endif()

set(TARGET_LIBS ${ara_sdk_video_LIBRARIES})

list(APPEND TARGET_LIBS GLBase SceneGraph AudioVideo UI Utilities)
if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

include(ResCompInclude)

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})

#gen_android_proj(${PROJECT_NAME})
