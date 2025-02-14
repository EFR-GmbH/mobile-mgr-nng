
set(_GLIB_ROOT_HINTS ${GLIB_ROOT_DIR} ENV GLIB_ROOT_DIR)

include(FindPackageHandleStandardArgs)


find_library(GLIB_LIBRARY
    NAMES glib-2.0
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES lib
)

get_filename_component(_GLIB_LIBRARY_DIR ${GLIB_LIBRARY} PATH)

find_library(GIO_LIBRARY
    NAMES gio-2.0
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES lib
)

find_library(GOBJECT_LIBRARY
    NAMES gobject-2.0
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES lib
)

find_path(GLIB_INCLUDE_DIR
    NAMES glib.h
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES glib-2.0
)

find_path(GIO_INCLUDE_DIR
    NAMES gio.h
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES glib-2.0/gio
)

find_path(GOBJECT_INCLUDE_DIR
    NAMES gobject.h
    HINTS ${_GLIB_ROOT_HINTS}
    PATH_SUFFIXES glib-2.0/gobject
)

find_path(GLIBCONFIG_INCLUDE_DIR
    NAMES glibconfig.h
    HINTS ${_GLIB_ROOT_HINTS} ${_GLIB_LIBRARY_DIR}
    PATH_SUFFIXES glib-2.0/include
)

if (${GLIB_LIBRARY_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find GLIB library")
endif()

if (${GIO_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find GIO library")
endif()

if (${GOBJECT_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find GOBJECT library")
endif()

mark_as_advanced(
    GLIB_INCLUDE_DIR
    GIO_INCLUDE_DIR
    GOBJECT_INCLUDE_DIR
    GLIBCONFIG_INCLUDE_DIR
    GLIB_LIBRARY
    GIO_LIBRARY
    GOBJECT_LIBRARY
)

find_package_handle_standard_args("glib-2.0" REQUIRED_VARS
    GLIB_INCLUDE_DIR
    GIO_INCLUDE_DIR
    GOBJECT_INCLUDE_DIR
    GLIBCONFIG_INCLUDE_DIR
    GLIB_LIBRARY
    GIO_LIBRARY
    GOBJECT_LIBRARY
)

set(GLIB_INCLUDE_DIRS
    ${GLIB_INCLUDE_DIR}
    ${GIO_INCLUDE_DIR}
    ${GOBJECT_INCLUDE_DIR}
    ${GLIBCONFIG_INCLUDE_DIR}
)

set(GLIB_LIBRARIES
    ${GLIB_LIBRARY}
    ${GIO_LIBRARY}
    ${GOBJECT_LIBRARY}
)
