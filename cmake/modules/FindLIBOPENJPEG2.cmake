# - Try to find the libopenjpeg2 library
# Once done this will define
#
#  LIBOPENJPEG2_FOUND - system has libopenjpeg
#  LIBOPENJPEG2_INCLUDE_DIRS - the libopenjpeg include directories
#  LIBOPENJPEG2_LIBRARIES - Link these to use libopenjpeg

# Copyright (c) 2008, Albert Astals Cid, <aacid@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.


set(LIBOPENJPEG2_FOUND FALSE)
set(LIBOPENJPEG2_INCLUDE_DIRS)
set(LIBOPENJPEG2_LIBRARIES)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBOPENJPEG2 libopenjp2)

if (LIBOPENJPEG2_FOUND)
  add_definitions(-DUSE_OPENJPEG2)
  # This make full path for the library
  find_library(LIBOPENJPEG2_LIBRARY NAMES ${LIBOPENJPEG2_LIBRARIES}
               HINTS ${LIBOPENJPEG2_LIBRARY_DIRS}
  )
  set(LIBOPENJPEG2_LIBRARIES "${LIBOPENJPEG2_LIBRARY}")

  find_path(LIBOPENJPEG2_INCLUDE_DIR openjpeg.h
            HINTS ${LIBOPENJPEG2_INCLUDE_DIRS}
  )
  set(LIBOPENJPEG2_INCLUDE_DIRS "${LIBOPENJPEG2_INCLUDE_DIR}")

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(LIBOPENJPEG2 DEFAULT_MSG LIBOPENJPEG2_LIBRARIES LIBOPENJPEG2_INCLUDE_DIRS)
  mark_as_advanced(
    LIBOPENJPEG2_INCLUDE_DIR
    LIBOPENJPEG2_LIBRARIES
  )
endif ()
