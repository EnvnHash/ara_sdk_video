cmake_minimum_required(VERSION 3.12)

project(NDISimpleTestSender VERSION 0.1.0 DESCRIPTION "NDI Test Sender")

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)

include(VisualStudioSettings)
include(GeneralSettings)

file(GLOB SOURCES *.cpp *.hpp)

if(${CMAKE_BUILD_TYPE} MATCHES Debug)
	add_executable(${PROJECT_NAME} ${SOURCES})
else()
	if (WIN32)
		add_executable(${PROJECT_NAME}  ${SOURCES})
		#add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
	else()
		add_executable(${PROJECT_NAME} ${SOURCES})
	endif()
endif()

set(TARGET_LIBS  Utilities)
if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
	list(APPEND TARGET_LIBS resources)
endif()

if (WIN32)
	target_link_libraries(${PROJECT_NAME} ${TARGET_LIBS})
elseif (UNIX)
	target_link_libraries(${PROJECT_NAME} ${TARGET_LIBS} pthread)
endif ()

include(GLBaseCopyDependencies)
include(Gtk3Include)
include(PugiXMLInclude)
include(UtilitiesDependencies)
include(CMakeRC)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputDependencies.cmake)
include(${ARA_AV_SOURCE_DIR}/cmake/Modules/VideoInputCopyDependencies.cmake)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Assets/resdata/FullHD_Pattern.png ${CMAKE_CURRENT_BINARY_DIR}
	)

if (WIN32)
set(Stage3D_3RD_PARTY_LIB_DIR ${CMAKE_SOURCE_DIR}/Libraries/third_party)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy ${Stage3D_3RD_PARTY_LIB_DIR}/NDI/Bin/${LIB_ARCH_PATH}/Processing.NDI.Lib.${LIB_ARCH_PATH}.dll $<TARGET_FILE_DIR:${PROJECT_NAME}>
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Libraries/third_party/NDI/Bin/${LIB_ARCH_PATH}/Processing.NDI.Lib.${LIB_ARCH_PATH}.dll ${CMAKE_CURRENT_BINARY_DIR}
)
endif(WIN32)

INSTALL(TARGETS ${PROJECT_NAME}
		DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)