cmake_minimum_required(VERSION 3.12)

project(FFmpegReencode)

set(SOURCES main.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/AraSdk.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/AraSdkVideo.cmake)

include_directories(
	${ara_sdk_INCLUDE_DIRS}
	${ara_sdk_video_INCLUDE_DIRS}
)

set(TARGET_LIBS ${ara_sdk_video_LIBRARIES} ${ara_sdk_LIBRARIES} GLBase SceneGraph AudioVideo Utilities)

if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

target_link_libraries (${PROJECT_NAME} ${TARGET_LIBS})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Assets/videos/trailer_1080p.mov ${CMAKE_CURRENT_BINARY_DIR}
)

INSTALL(TARGETS ${PROJECT_NAME}
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)