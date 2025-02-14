cmake_minimum_required(VERSION "${CMAKE_VERSION}")

project(mbim_nng VERSION 1.0.0 LANGUAGES C)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set(SRC_FOLDER "${PROJECT_SOURCE_DIR}/src")

option(SAMPLE_CLIENT "Build sample client" OFF)

find_package(nng REQUIRED)
find_package(mbim-glib REQUIRED)
find_package(qmi-glib REQUIRED)
find_package(glib-2.0 REQUIRED)

include_directories(
    ${NNG_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${MBIM_GLIB_INCLUDE_DIRS}
)

add_executable(${PROJECT_NAME}
    ${SRC_FOLDER}/databuf.c
    ${SRC_FOLDER}/mbim.c
    ${SRC_FOLDER}/qmi.c
    ${SRC_FOLDER}/nng_server.c
    ${SRC_FOLDER}/main.c
)

target_link_libraries(${PROJECT_NAME}
    ${NNG_LIBRARIES}
    ${MBIM_GLIB_LIBRARIES}
    ${GLIB_LIBRARIES}
)

if(SAMPLE_CLIENT)
    set(S_CLIENT "sample_client")
    include_directories(${SRC_FOLDER})
    add_executable(${S_CLIENT}
        ${SRC_FOLDER}/databuf.c
        ${PROJECT_SOURCE_DIR}/sample/client.c
    )
    target_link_libraries(${S_CLIENT} ${NNG_LIBRARIES})
endif()
