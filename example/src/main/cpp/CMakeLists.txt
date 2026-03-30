cmake_minimum_required(VERSION 3.16)
project(example_driver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-command-line-argument")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")

find_library(hilog-lib hilog_ndk.z)
find_library(napi-lib ace_napi.z)

set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/mbedtls
    ${CMAKE_CURRENT_BINARY_DIR}/mbedtls
    EXCLUDE_FROM_ALL)

set(CRYPTO_BACKEND "mbedTLS" CACHE STRING "" FORCE)
set(MBEDTLS_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/mbedtls/include
    CACHE PATH ""
    FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/libssh2
    ${CMAKE_CURRENT_BINARY_DIR}/libssh2
    EXCLUDE_FROM_ALL)

set(SOURCES
    example_driver.cpp
    pty/pty_handler.cpp
    ssh/ssh_session.cpp
)

add_library(example_driver SHARED ${SOURCES})

target_include_directories(example_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/pty
    ${CMAKE_CURRENT_SOURCE_DIR}/ssh
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/libssh2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/mbedtls/include
)

target_link_libraries(example_driver PRIVATE
    libssh2_static
    mbedcrypto
    mbedx509
    mbedtls
    ${hilog-lib}
    ${napi-lib}
)
