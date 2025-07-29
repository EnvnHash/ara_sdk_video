cmake_minimum_required(VERSION 3.6.2)
project(VideoInput_UnitTests VERSION 0.1 LANGUAGES CXX)

set(CMAKE_MODULE_PATH ${ARA_SDK_SOURCE_DIR}/Libraries/cmake/Modules)

include(GeneralSettings)
include(VisualStudioSettings)
include(GoogleTest)

enable_testing()

include_directories(
    ${GTEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/Libraries/VideoInput/src
    ${CMAKE_SOURCE_DIR}/Libraries/Utilities/src
    ${CMAKE_SOURCE_DIR}/Libraries/third_party/nameof
)

# add test source files
file(GLOB VIDEOINPUT_UT_SRC *.cpp)

add_executable(${PROJECT_NAME} ${VIDEOINPUT_UT_SRC})

# automatically scan for gtest macros in target
gtest_add_tests(TARGET      ${PROJECT_NAME}
                TEST_SUFFIX .noArgs
                TEST_LIST   noArgsTests
)

set(TARGET_LIBS gtest_main gmock_main GLBase GLSceneGraph VideoInput Utilities)
if(NOT ${CMAKE_BUILD_TYPE} MATCHES Debug OR ANDROID)
    list(APPEND TARGET_LIBS resources)
endif()

target_link_libraries(${PROJECT_NAME} ${TARGET_LIBS} )

if (NOT ANDROID)
   # target_precompile_headers(${PROJECT_NAME} PRIVATE ${ARA_SDK_SOURCE_DIR}/Libraries/VideoInput/src/videoinput_common.h)
endif()

include(VideoInputCopyDependencies)
include(VideoInputDependencies)
include(GLBaseDependencies)
include(UtilitiesDependencies)
include(GLBaseCopyDependencies)

INSTALL(TARGETS ${PROJECT_NAME}
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR} COMPONENT ${PROJECT_NAME} OPTIONAL)