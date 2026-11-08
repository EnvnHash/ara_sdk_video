# vim: ts=2 sw=2
# - Try to find the required ffmpeg components(default: AVFORMAT, AVUTIL, AVCODEC)
#
# Once done this will define
#  FFMPEG_FOUND         - System has the all required components.
#  FFMPEG_INCLUDE_DIRS  - Include directory necessary for using the required components headers.
#  FFMPEG_LIBRARIES     - Link these to use the required ffmpeg components.
#  FFMPEG_DEFINITIONS   - Compiler switches required for using the required ffmpeg components.
#
# For each of the components it will additionally set.
#   - AVCODEC
#   - AVDEVICE
#   - AVFORMAT
#   - AVFILTER
#   - AVUTIL
#   - POSTPROC
#   - SWSCALE
# the following variables will be defined
#  <component>_FOUND        - System has <component>
#  <component>_INCLUDE_DIRS - Include directory necessary for using the <component> headers
#  <component>_LIBRARIES    - Link these to use <component>
#  <component>_DEFINITIONS  - Compiler switches required for using <component>
#  <component>_VERSION      - The components version
#
# Copyright (c) 2006, Matthias Kretz, <kretz@kde.org>
# Copyright (c) 2008, Alexander Neundorf, <neundorf@kde.org>
# Copyright (c) 2011, Michael Jansen, <kde@michael-jansen.biz>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

include(FindPackageHandleStandardArgs)

# The default components were taken from a survey over other FindFFMPEG.cmake files
if (NOT FFMPEG_FIND_COMPONENTS)
  set(FFMPEG_FIND_COMPONENTS AVCODEC AVFORMAT AVFILTER AVUTIL AVDEVICE SWSCALE POSTPROC SWRESAMPLE)
endif ()

#
### Macro: set_component_found
#
# Marks the given component as found if both *_LIBRARIES AND *_INCLUDE_DIRS is present.
#
macro(set_component_found _component )
  if (PC_${_component}_LIBRARIES AND PC_${_component}_INCLUDE_DIRS)
    #message(STATUS "  - ${_component} found.")
    set(${_component}_FOUND TRUE)
  else ()
    message(STATUS "  - ${_component} not found.")
  endif ()
endmacro()

#
### Macro: find_component
#
# Checks for the given component by invoking pkgconfig and then looking up the libraries and
# include directories.
#
macro(find_component _component _pkgconfig _library _header)

  if (NOT WIN32)
     # use pkg-config to get the directories and then use these values
     # in the FIND_PATH() and FIND_LIBRARY() calls
     find_package(PkgConfig)
     if (PKG_CONFIG_FOUND)
       pkg_check_modules(PC_${_component} REQUIRED ${_pkgconfig})
       if (NOT ${PC_${_component}_FOUND})
         message(ERROR "pkgconfig could not find ${_pkgconfig}")
       else()
         #message(STATUS "pkgconfig found: ${_pkgconfig}")
         message(STATUS "pkgconfig found libs: ${PC_${_component}_LIBRARIES}")
         #message(STATUS "pkgconfig found include dir: ${PC_${_component}_INCLUDE_DIRS}")
         #message(STATUS "pkgconfig found include cflags: ${PC_${_component}_CFLAGS}")
       endif()
     else()
       message(STATUS PkgConfig not found)
     endif()
  endif (NOT WIN32)

  #message(STATUS "find_path ${_component}_INCLUDE_DIRS ${_header} ${PC_${_component}_INCLUDE_DIRS}")

  find_path(${_component}_INCLUDE_DIRS ${_header}
    HINTS
      ${PC_LIB${_component}_INCLUDEDIR}
      ${PC_LIB${_component}_INCLUDE_DIRS}
      ${PC_${_component}_INCLUDE_DIRS}
    PATH_SUFFIXES
      FFMpeg
  )

  find_library(${_component}_LIBRARIES NAMES ${_library}
      HINTS
      ${PC_${_component}_LIBRARY_DIRS}
      ${PC_LIB${_component}_LIBDIR}
      ${PC_LIB${_component}_LIBRARY_DIRS}
  )

  set(${_component}_DEFINITIONS  ${PC_${_component}_CFLAGS_OTHER} CACHE STRING "The ${_component} CFLAGS.")
  set(${_component}_VERSION      ${PC_${_component}_VERSION}      CACHE STRING "The ${_component} version number.")

  set_component_found(${_component})

  mark_as_advanced(
    ${_component}_INCLUDE_DIRS
    ${_component}_LIBRARIES
    ${_component}_DEFINITIONS
    ${_component}_VERSION)

endmacro()

set(SKIP_LIBPOSTPROC OFF)

if(UNIX AND NOT APPLE)
  if(EXISTS "/etc/os-release")
    file(READ "/etc/os-release" OS_RELEASE_CONTENT)
    # Check if the distribution is Ubuntu
    if(OS_RELEASE_CONTENT MATCHES "ID=ubuntu")
      # Extract VERSION_ID (e.g., "26.04")
      string(REGEX MATCH "VERSION_ID=\"([0-9]+\\.[0-9]+)\"" _ ${OS_RELEASE_CONTENT})
      set(UBUNTU_VERSION "${CMAKE_MATCH_1}")

      if(UBUNTU_VERSION VERSION_GREATER_EQUAL "26.04")
        message(STATUS "Detected Ubuntu ${UBUNTU_VERSION} (>= 26.04): disabling libpostproc")
        # Set your flags or variables here
        set(SKIP_LIBPOSTPROC ON)
      endif()
    endif()
  endif()
endif()


# Check for cached results. If there are skip the costly part.
if (NOT FFMPEG_LIBRARIES)

  # Check for all possible component.
  find_component(AVCODEC    libavcodec    avcodec  libavcodec/avcodec.h)
  find_component(AVFORMAT   libavformat   avformat libavformat/avformat.h)
  find_component(AVDEVICE   libavdevice   avdevice libavdevice/avdevice.h)
  find_component(AVUTIL     libavutil     avutil   libavutil/avutil.h)
  find_component(AVFILTER   libavfilter   avfilter libavfilter/avfilter.h)
  find_component(SWSCALE    libswscale    swscale  libswscale/swscale.h)
  find_component(SWRESAMPLE libswresample swresample libswresample/swresample.h)

  if (NOT ${SKIP_LIBPOSTPROC})
    find_component(POSTPROC   libpostproc   postproc libpostproc/postprocess.h)
  endif()

  # Check if the required components were found and add their stuff to the FFMPEG_* vars.
  set(FFMPEG_ALL_COMP_FOUND TRUE)

  foreach (_component ${FFMPEG_FIND_COMPONENTS})
    if (${_component}_FOUND)
      message(STATUS "Required component ${_component} ${${_component}_LIBRARIES} present.")
      set(FFMPEG_LIBRARIES   ${FFMPEG_LIBRARIES}   ${${_component}_LIBRARIES})
      set(FFMPEG_DEFINITIONS ${FFMPEG_DEFINITIONS} ${${_component}_DEFINITIONS})

      list(FIND FFMPEG_INCLUDE_DIRS ${${_component}_INCLUDE_DIRS} FFMPEG_INCLUDE_DIRS_CONTAINS_ITEM)
      if (${FFMPEG_INCLUDE_DIRS_CONTAINS_ITEM} STREQUAL "-1")
        list(APPEND FFMPEG_INCLUDE_DIRS ${${_component}_INCLUDE_DIRS})
      endif()
    else()
      set(FFMPEG_ALL_COMP_FOUND FALSE)
      message(STATUS "Required component ${_component} missing.")
    endif()
  endforeach()

  if (FFMPEG_ALL_COMP_FOUND)
    set(FFMPEG_FOUND ON)
  endif()

  # Build the include path with duplicates removed.
  if (FFMPEG_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
  endif ()

  # cache the vars.
  set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} CACHE STRING "The FFmpeg include directories." FORCE)
  set(FFMPEG_LIBRARIES    ${FFMPEG_LIBRARIES}    CACHE STRING "The FFmpeg libraries." FORCE)
  set(FFMPEG_DEFINITIONS  ${FFMPEG_DEFINITIONS}  CACHE STRING "The FFmpeg cflags." FORCE)

  mark_as_advanced(FFMPEG_INCLUDE_DIRS
                   FFMPEG_LIBRARIES
                   FFMPEG_DEFINITIONS)
else()
  set(FFMPEG_FOUND ON)
  mark_as_advanced(FFMPEG_INCLUDE_DIRS
          FFMPEG_LIBRARIES
          FFMPEG_DEFINITIONS)
  foreach (_component ${FFMPEG_FIND_COMPONENTS})
    mark_as_advanced(
            ${_component}_INCLUDE_DIRS
            ${_component}_LIBRARIES
            ${_component}_DEFINITIONS
            ${_component}_VERSION)
  endforeach()

endif ()

set(FFMPEG_COMPONENT_LIST AVCODEC AVDEVICE AVFILTER AVFORMAT AVUTIL SWSCALE)
if (NOT ${SKIP_LIBPOSTPROC})
  list(_component APPEND POSTPROC)
endif()

# Now set the noncached _FOUND vars for the components.
foreach (_component ${FFMPEG_COMPONENT_LIST})
  set_component_found(${_component})
endforeach ()

# Compile the list of required vars
set(_FFMPEG_REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)
foreach (_component ${FFMPEG_FIND_COMPONENTS})
  list(APPEND _FFMPEG_REQUIRED_VARS ${_component}_LIBRARIES ${_component}_INCLUDE_DIRS)
endforeach ()

# Give a nice error message if some of the required vars are missing.
#find_package_handle_standard_args(FFMpeg DEFAULT_MSG ${_FFMPEG_REQUIRED_VARS})
