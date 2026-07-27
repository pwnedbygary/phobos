import os
import sys

# Ensure script runs relative to repo root regardless of invocation directory
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Find all thirdparty subdirectories (ymfm, sljit, etc.)
thirdparty_dirs = ["thirdparty"]
if os.path.exists("thirdparty"):
    for d in os.listdir("thirdparty"):
        full_p = os.path.join("thirdparty", d)
        if os.path.isdir(full_p):
            thirdparty_dirs.append(full_p.replace("\\", "/"))

print(f"📦 Found thirdparty directories: {thirdparty_dirs}")

# 2. Check and stub ares/resource/resource.hpp if missing
resource_hpp_dir = os.path.join("ares", "resource")
resource_hpp_path = os.path.join(resource_hpp_dir, "resource.hpp")

if not os.path.exists(resource_hpp_path):
    os.makedirs(resource_hpp_dir, exist_ok=True)
    with open(resource_hpp_path, "w", encoding="utf-8") as f:
        f.write("""#pragma once
#include <nall/vector.hpp>
#include <nall/string.hpp>

namespace ares::Resource {
    // Lightweight stub resources for headless Android build
    inline static const nall::vector<uint8_t> Logo{};
}
""")
    print("✏️ Created stub ares/resource/resource.hpp.")
else:
    print("ℹ️ Ares resource.hpp already exists.")

# 3. Gather Unity sources
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

# Add ymfm C++ source if present
ymfm_cpp = os.path.join("thirdparty", "ymfm", "ymfm.cpp")
if os.path.exists(ymfm_cpp):
    sources.append(ymfm_cpp.replace("\\", "/"))

sources_str = "\n    ".join(sources)

# 4. Generate CMake include paths
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/ares",
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
    "${CMAKE_CURRENT_SOURCE_DIR}/mia",
    "${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp",
]

for tp in thirdparty_dirs:
    include_dirs.append(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{tp}")

include_dirs_str = "\n    ".join(include_dirs)

cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(BUILD_RELEASE)

# Suppress harmless warnings
add_compile_options(-Wno-shift-op-parentheses -Wno-parentheses -Wno-narrowing -Wno-macro-redefined -Wno-deprecated-anon-enum-enum-conversion)

include_directories(
    {include_dirs_str}
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

print("✏️ Updated CMakeLists.txt with full thirdparty paths.")

# 5. Git Commit
os.system("git add .")
os.system('git commit -m "fix: Add ymfm/thirdparty include paths and stub resource.hpp"')
print("\n✅ Patches applied successfully!")
