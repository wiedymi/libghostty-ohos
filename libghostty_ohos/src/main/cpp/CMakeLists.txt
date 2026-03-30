cmake_minimum_required(VERSION 3.16)
project(libghostty_ohos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DVK_USE_PLATFORM_OHOS)
add_definitions(-DLOG_DOMAIN=0x0001)

# Find required libraries
find_library(hilog-lib hilog_ndk.z)
find_library(ace-lib ace_ndk.z)
find_library(napi-lib ace_napi.z)
find_library(rawfile-lib rawfile.z)
find_library(native-window-lib native_window)
find_library(native-buffer-lib native_buffer)
find_library(native-fence-lib native_fence)
find_library(native-drawing-lib native_drawing)
find_library(inputmethod-lib ohinputmethod)

# Source files
set(SOURCES
    napi_init.cpp
    terminal/terminal.cpp
    terminal/terminal_state.cpp
    terminal/theme.cpp
    renderer/native_drawing_renderer.cpp
)

# Create shared library
add_library(libghostty_ohos SHARED ${SOURCES})

# Include directories
target_include_directories(libghostty_ohos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/terminal
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/pty
    ${CMAKE_CURRENT_SOURCE_DIR}/ssh
)

# libghostty-vt static library (REQUIRED)
set(GHOSTTY_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../prebuilt/${OHOS_ARCH}/libghostty_vt.a")
if(NOT EXISTS "${GHOSTTY_LIB}")
    message(FATAL_ERROR
        "libghostty_vt.a not found at ${GHOSTTY_LIB}\n"
        "Build libghostty-vt from https://github.com/ghostty-org/ghostty:\n"
        "  cd ghostty\n"
        "  zig build -Dtarget=aarch64-freestanding -Doptimize=ReleaseFast\n"
        "Then copy zig-out/lib/libghostty_vt.a to prebuilt/arm64-v8a/")
endif()

add_library(ghostty_vt STATIC IMPORTED)
set_target_properties(ghostty_vt PROPERTIES
    IMPORTED_LOCATION ${GHOSTTY_LIB}
)

# Link libraries
target_link_libraries(libghostty_ohos PUBLIC
    ghostty_vt
    ${hilog-lib}
    ${ace-lib}
    ${napi-lib}
    ${rawfile-lib}
    ${native-drawing-lib}
    ${native-buffer-lib}
    ${native-fence-lib}
    ${native-window-lib}
    ${inputmethod-lib}
)
