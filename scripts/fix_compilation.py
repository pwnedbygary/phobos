import os

def fix_cmake():
    sources = []
    
    # 1. Add nall root (if it exists)
    if os.path.exists("nall/nall.cpp"):
         sources.append("nall/nall.cpp")
         
    # 2. Add mia root
    if os.path.exists("mia/mia.cpp"):
         sources.append("mia/mia.cpp")
         
    # 3. Add ares cores (Only exact unity root files: ares/sfc/sfc.cpp)
    if os.path.exists("ares"):
        for item in os.listdir("ares"):
            sys_dir = os.path.join("ares", item)
            if os.path.isdir(sys_dir):
                sys_cpp = os.path.join(sys_dir, f"{item}.cpp")
                if os.path.exists(sys_cpp):
                    sources.append(sys_cpp.replace("\\", "/"))

    sources_str = "\n    ".join(sources)
    
    cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(BUILD_RELEASE)

# Suppress harmless warnings that heavily clutter the Android Studio output
add_compile_options(-Wno-shift-op-parentheses -Wno-parentheses -Wno-narrowing -Wno-macro-redefined)

include_directories(
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

# Precisely targeted Unity builds
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
    print("✏️ Re-wrote CMakeLists.txt to perfectly match Ares Unity constraints.")


def fix_gradle():
    gradle_path = "android/app/build.gradle"
    with open(gradle_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    if "ndkVersion" not in content:
        # Insert ndkVersion right after compileSdk
        content = content.replace(
            "compileSdk 34", 
            "compileSdk 34\n    ndkVersion \"26.1.10909125\" // Required for C++20 <ranges>"
        )
        
        with open(gradle_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✏️ Forced NDK 26 in app/build.gradle.")
    else:
        print("⚡ NDK version already set.")

if __name__ == "__main__":
    fix_cmake()
    fix_gradle()
    
    # Auto commit
    os.system("git add .")
    os.system('git commit -m "fix: Corrected CMake Unity build constraints and NDK 26 requirements"')
    
    print("\\n✅ Build system patched!")
