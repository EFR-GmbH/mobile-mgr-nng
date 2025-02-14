
set(_MBIM_GLIB_ROOT_HINTS ${MBIM-GLIB_ROOT_DIR} ENV MBIM-GLIB_ROOT_DIR)

include(FindPackageHandleStandardArgs)

find_path(MBIM_GLIB_INCLUDE_DIR
    NAMES libmbim-glib.h
    HINTS ${_MBIM_GLIB_ROOT_HINTS}
    PATH_SUFFIXES libmbim-glib
)

find_library(MBIM_GLIB_LIBRARY
    NAMES mbim-glib
    HINTS ${_MBIM_GLIB_ROOT_HINTS}
    PATH_SUFFIXES lib
)

if (${MBIM_GLIB_LIBRARY_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find MBIM library")
endif()

mark_as_advanced(
    MBIM_GLIB_INCLUDE_DIR
    MBIM_GLIB_LIBRARY
)

find_package_handle_standard_args("mbim-glib" REQUIRED_VARS MBIM_GLIB_LIBRARY MBIM_GLIB_INCLUDE_DIR)

set(MBIM_GLIB_INCLUDE_DIRS ${MBIM_GLIB_INCLUDE_DIR})
set(MBIM_GLIB_LIBRARIES ${MBIM_GLIB_LIBRARY})
