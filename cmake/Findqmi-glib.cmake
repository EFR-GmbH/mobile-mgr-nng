
set(_QMI_GLIB_ROOT_HINTS ${QMI-GLIB_ROOT_DIR} ENV QMI-GLIB_ROOT_DIR)

include(FindPackageHandleStandardArgs)

find_path(QMI_GLIB_INCLUDE_DIR
    NAMES libqmi-glib.h
    HINTS ${_QMI_GLIB_ROOT_HINTS}
    PATH_SUFFIXES libqmi-glib
)

find_library(QMI_GLIB_LIBRARY
    NAMES qmi-glib
    HINTS ${_QMI_GLIB_ROOT_HINTS}
    PATH_SUFFIXES lib
)

if (${QMI_GLIB_LIBRARY_LIBRARY-NOTFOUND})
    message(FATAL_ERROR "Failed to find QMI library")
endif()

mark_as_advanced(
    QMI_GLIB_INCLUDE_DIR
    QMI_GLIB_LIBRARY
)

find_package_handle_standard_args("qmi-glib" REQUIRED_VARS QMI_GLIB_LIBRARY QMI_GLIB_INCLUDE_DIR)

set(QMI_GLIB_INCLUDE_DIRS ${QMI_GLIB_INCLUDE_DIR})
set(QMI_GLIB_LIBRARIES ${QMI_GLIB_LIBRARY})
