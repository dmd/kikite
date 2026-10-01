cmake_minimum_required(VERSION 3.13)
project(kikite_render C CXX)

set(PAX_VERSION_IS_SNAPSHOT false)
set(PAX_VERSION_MAJOR 2)
set(PAX_VERSION_MINOR 2)
set(PAX_VERSION_PATCH 2)
set(PAX_VERSION 2.2.2)
set(_PAX_STATIC_OR_SHARED STATIC)
set(PAX_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/robotman2412__pax-gfx)
include(${PAX_ROOT}/core/CMakeLists.txt)

set(MAIN ${CMAKE_CURRENT_LIST_DIR}/../../../main)
add_executable(render
    render.c
    ${MAIN}/ui.c
    ${MAIN}/view_spectrum.c
    ${MAIN}/view_listen.c
    ${MAIN}/view_devices.c
    ${MAIN}/view_radio.c
    ${MAIN}/spectrum.c
    ${MAIN}/capture.c
    ${MAIN}/decode.c
    ${MAIN}/meshtastic.c
    ${CMAKE_CURRENT_LIST_DIR}/../aes_ctr_openssl.c
    ${MAIN}/lorawan_netid.c
    ${MAIN}/profiles.c
)
target_include_directories(render PRIVATE ${CMAKE_CURRENT_LIST_DIR}/shims ${MAIN})
target_compile_options(render PRIVATE -Wall -Wno-unused-parameter -fsanitize=address,undefined)
target_link_options(render PRIVATE -fsanitize=address,undefined)
execute_process(COMMAND brew --prefix openssl@3 OUTPUT_VARIABLE OPENSSL_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
target_include_directories(render PRIVATE ${OPENSSL_PREFIX}/include)
target_link_libraries(render pax_gfx ${OPENSSL_PREFIX}/lib/libcrypto.dylib)
target_include_directories(pax_gfx PRIVATE ${CMAKE_CURRENT_LIST_DIR}/compat)
target_compile_options(pax_gfx PRIVATE -w)
