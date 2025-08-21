cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0146 NEW)

project(AudioVideo)
set(CMAKE_CXX_STANDARD 20)

set (ARA_AV_IS_SUBPROJECT OFF)
if (NOT ARA_AV_SOURCE_DIR)
    set(ARA_AV_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})
    if (NOT ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
        set (ARA_AV_IS_SUBPROJECT ON)
        message(STATUS "ARA AUDIO/VIDEO include as external dependency")
    else ()
        message(STATUS "ARA AUDIO/VIDEO is main project")
    endif()
endif()

if (NOT ARA_AV_IS_SUBPROJECT)
    include(FetchContent)
    Set(FETCHCONTENT_QUIET FALSE)

    message(STATUS "build ara sdk - checking for updates")
    FetchContent_Declare(
        ara_sdk
        GIT_REPOSITORY https://github.com/envnhash/ara_sdk.git
        GIT_TAG master
        GIT_PROGRESS ON
    )
    FetchContent_MakeAvailable(ara_sdk)
endif()

include(${ARA_AV_SOURCE_DIR}/cmake/Modules/AraSdkVideo.cmake)

add_subdirectory(Examples)
add_subdirectory(src)
add_subdirectory(third_party)
#add_subdirectory(UnitTests)

# pass ara_sdk dependencies variables to parent project in case it exists
get_directory_property(hasParent PARENT_DIRECTORY)
if (hasParent)
    set(ara_sdk_video_COMPILE_DEFINITIONS "${ara_sdk_video_COMPILE_DEFINITIONS}" PARENT_SCOPE)
    set(ara_sdk_video_INCLUDE_DIRS "${ara_sdk_video_INCLUDE_DIRS}" PARENT_SCOPE)
    set(ara_sdk_video_LIBRARIES "${ara_sdk_video_LIBRARIES}" PARENT_SCOPE)
    set(ara_sdk_video_BINARIES "${ara_sdk_video_BINARIES}" PARENT_SCOPE)
endif()