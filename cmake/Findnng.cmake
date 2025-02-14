
set(_NNG_ROOT_HINTS ${NNG_ROOT_DIR} ENV NNG_ROOT_DIR)

include(FindPackageHandleStandardArgs)

find_path(NNG_INCLUDE_DIR
    NAMES nng.h
    HINTS ${_NNG_ROOT_HINTS}
    PATH_SUFFIXES nng
)

find_library(NNG_LIBRARY
    NAMES nng
    HINTS ${_NNG_ROOT_HINTS}
    PATH_SUFFIXES lib
)

if (${NNG_LIBRARY_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find NNG library")
endif()

find_package_handle_standard_args(nng REQUIRED_VARS NNG_LIBRARY NNG_INCLUDE_DIR)

mark_as_advanced(
    NNG_INCLUDE_DIR
    NNG_LIBRARY
)

get_filename_component(_NNG_LIBRARY_DIR ${NNG_LIBRARY} PATH)

set(NNG_INCLUDE_DIRS ${NNG_INCLUDE_DIR} ${_NNG_LIBRARY_DIR})
set(NNG_LIBRARIES ${NNG_LIBRARY})
