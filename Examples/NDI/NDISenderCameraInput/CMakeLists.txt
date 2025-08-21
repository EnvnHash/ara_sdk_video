cmake_minimum_required(VERSION 3.12)

project(NDISenderCameraInput)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

set(TARGET_LIBS ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)

if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})

INSTALL(TARGETS ${PROJECT_NAME}
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)