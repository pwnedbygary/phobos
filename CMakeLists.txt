cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(WARNING "This project is heavily optimized for arm64-v8a. Proceed with caution.")
endif()

add_compile_definitions(BUILD_RELEASE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/ares
    ${CMAKE_CURRENT_SOURCE_DIR}/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/mia
    ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp
)

file(GLOB_RECURSE NALL_SOURCES "nall/*.cpp")
file(GLOB_RECURSE MIA_SOURCES "mia/*.cpp")
file(GLOB_RECURSE ARES_SOURCES "ares/*.cpp")

set(LIBCO_SOURCE "libco/aarch64.c")

set(ANDROID_SOURCES 
    android/app/src/main/cpp/PhobosRunner.cpp 
    android/app/src/main/cpp/PhobosJNI.cpp
)

add_library(phobos_android SHARED 
    ${NALL_SOURCES}
    ${MIA_SOURCES}
    ${ARES_SOURCES}
    ${LIBCO_SOURCE}
    ${ANDROID_SOURCES}
)

target_link_libraries(phobos_android android aaudio log)
