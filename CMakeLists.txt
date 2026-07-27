cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(BUILD_RELEASE)

# Suppress harmless warnings
add_compile_options(-Wno-shift-op-parentheses -Wno-parentheses -Wno-narrowing -Wno-macro-redefined -Wno-deprecated-anon-enum-enum-conversion)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ares
    ${CMAKE_CURRENT_SOURCE_DIR}/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/mia
    ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/GL
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/KHR
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/TZXFile
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/jansson/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/lzma-25.01/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/lzma-25.01/include/real
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/miniz-3.1.1
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/zstd-1.5.7
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/include/dr_libs
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/include/libchdr
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/librashader/include/librashader
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qon
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit/regex_src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit/sljit_src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit/test_src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm/examples/vgmrender
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm/src
)

set(LIBCO_SOURCE "libco/aarch64.c")

set(ANDROID_SOURCES 
    android/app/src/main/cpp/PhobosRunner.cpp 
    android/app/src/main/cpp/PhobosJNI.cpp
)

set(ARES_UNIFIED_SOURCES
    mia/mia.cpp
    ares/a26/a26.cpp
    ares/cv/cv.cpp
    ares/fc/fc.cpp
    ares/gb/gb.cpp
    ares/gba/gba.cpp
    ares/md/md.cpp
    ares/ms/ms.cpp
    ares/msx/msx.cpp
    ares/myvision/myvision.cpp
    ares/n64/n64.cpp
    ares/ng/ng.cpp
    ares/ngp/ngp.cpp
    ares/pce/pce.cpp
    ares/ps1/ps1.cpp
    ares/sfc/sfc.cpp
    ares/sg/sg.cpp
    ares/spec/spec.cpp
    ares/ws/ws.cpp
)

add_library(phobos_android SHARED 
    ${ARES_UNIFIED_SOURCES}
    ${LIBCO_SOURCE}
    ${ANDROID_SOURCES}
)

target_link_libraries(phobos_android android aaudio log)
