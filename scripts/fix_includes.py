import os

def fix_all():
    # 1. Create a tiny stub header for hiro in our C++ scaffolding folder
    hiro_dir = os.path.join("android", "app", "src", "main", "cpp", "hiro")
    os.makedirs(hiro_dir, exist_ok=True)
    hiro_hpp = os.path.join(hiro_dir, "hiro.hpp")
    
    with open(hiro_hpp, "w", encoding="utf-8") as f:
        f.write("""#pragma once
// Stub header for headless Android build (desktop hiro GUI toolkit removed)
namespace hiro {
    struct Application {};
    struct Window {};
    struct Widget {};
}
""")
    print("✏️ Created hiro/hiro.hpp stub for headless build.")

    # 2. Gather Unity sources for CMake
    sources = []
    if os.path.exists("nall/nall.cpp"):
         sources.append("nall/nall.cpp")
         
    if os.path.exists("mia/mia.cpp"):
         sources.append("mia/mia.cpp")
         
    if os.path.exists("ares"):
        for item in os.listdir("ares"):
            sys_dir = os.path.join("ares", item)
            if os.path.isdir(sys_dir):
                sys_cpp = os.path.join(sys_dir, f"{item}.cpp")
                if os.path.exists(sys_cpp):
                    sources.append(sys_cpp.replace("\\", "/"))

    sources_str = "\n    ".join(sources)

    # 3. Write updated CMakeLists.txt with ROOT directory included for libco
    cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(BUILD_RELEASE)

# Suppress harmless warnings
add_compile_options(-Wno-shift-op-parentheses -Wno-parentheses -Wno-narrowing -Wno-macro-redefined)

include_directories(
    ${{CMAKE_CURRENT_SOURCE_DIR}}
    ${{CMAKE_CURRENT_SOURCE_DIR}}/ares
    ${{CMAKE_CURRENT_SOURCE_DIR}}/nall
    ${{CMAKE_CURRENT_SOURCE_DIR}}/mia
    ${{CMAKE_CURRENT_SOURCE_DIR}}/thirdparty
    ${{CMAKE_CURRENT_SOURCE_DIR}}/thirdparty/sljit
    ${{CMAKE_CURRENT_SOURCE_DIR}}/android/app/src/main/cpp
)

set(LIBCO_SOURCE "libco/aarch64.c")

set(ANDROID_SOURCES 
    android/app/src/main/cpp/PhobosRunner.cpp 
    android/app/src/main/cpp/PhobosJNI.cpp
)

set(ARES_UNIFIED_SOURCES
    {sources_str}
)

add_library(phobos_android SHARED 
    ${{ARES_UNIFIED_SOURCES}}
    ${{LIBCO_SOURCE}}
    ${{ANDROID_SOURCES}}
)

target_link_libraries(phobos_android android aaudio log)
"""

    with open("CMakeLists.txt", "w", encoding="utf-8") as f:
        f.write(cmake_content)
    print("✏️ Updated CMakeLists.txt with root include directory.")

    # 4. Commit to Git
    os.system("git add .")
    os.system('git commit -m "fix: Add root include dir for libco and hiro stub header"')
    print("\n✅ Patches applied and committed!")

if __name__ == "__main__":
    fix_all()
