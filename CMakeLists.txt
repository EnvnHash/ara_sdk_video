cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0146 NEW)

project(AudioVideo)

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

    message(STATUS "build ara sdk lite - checking for updates")
    FetchContent_Declare(
        ara_sdk
        GIT_REPOSITORY https://github.com/envnhash/ara_sdk.git
        GIT_TAG master
        GIT_PROGRESS ON
    )
    FetchContent_MakeAvailable(ara_sdk)
    include(${ara_sdk_SOURCE_DIR}/Libraries/cmake/Modules/AraSdk.cmake)
endif()

add_subdirectory(Examples)
add_subdirectory(src)
add_subdirectory(third_party)
#add_subdirectory(UnitTests)
