import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Collect ALL subdirectories under thirdparty and ares/n64/vulkan containing headers
extra_includes = set()
extra_includes.add("thirdparty")

for search_base in ["thirdparty", os.path.join("ares", "n64", "vulkan")]:
    if os.path.exists(search_base):
        for root, dirs, files in os.walk(search_base):
            for f in files:
                if f.endswith(".h") or f.endswith(".hpp"):
                    rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                    extra_includes.add(rel_p)

# Explicitly ensure vulkan-headers/include AND vulkan-headers/include/vulkan are included
vulkan_headers_inc1 = "ares/n64/vulkan/parallel-rdp/vulkan-headers/include"
vulkan_headers_inc2 = "ares/n64/vulkan/parallel-rdp/vulkan-headers/include/vulkan"

if os.path.exists(vulkan_headers_inc1):
    extra_includes.add(vulkan_headers_inc1)
if os.path.exists(vulkan_headers_inc2):
    extra_includes.add(vulkan_headers_inc2)

sorted_extra_includes = sorted(list(extra_includes))

# 2. Gather Unity sources
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

ymfm_cpp = os.path.join("thirdparty", "ymfm", "ymfm.cpp")
if os.path.exists(ymfm_cpp):
    sources.append(ymfm_cpp.replace("\\", "/"))

sources_str = "\n    ".join(sources)

# 3. Generate CMakeLists.txt
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/ares",
    "${CMAKE_CURRENT_SOURCE_DIR}/mia",
    "${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp",
]

for inc in sorted_extra_includes:
    include_dirs.append(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{inc}")

include_dirs_str = "\n    ".join(include_dirs)

cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(BUILD_RELEASE VULKAN VK_NO_PROTOTYPES)

add_compile_options(
    -include "${{CMAKE_CURRENT_SOURCE_DIR}}/android/app/src/main/cpp/vulkan_android_compat.h"
    -Wno-shift-op-parentheses 
    -Wno-parentheses 
    -Wno-narrowing 
    -Wno-macro-redefined 
    -Wno-deprecated-anon-enum-enum-conversion
    -Wno-switch
)

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

target_link_libraries(phobos_android android aaudio log vulkan)
"""

with open("CMakeLists.txt", "w", encoding="utf-8") as f:
    f.write(cmake_content)

print("✏️ Updated CMakeLists.txt with explicit vulkan-headers/include/vulkan path.")

# 4. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Add explicit vulkan-headers/include/vulkan directory to CMakeLists.txt"')
print("\n✅ Fix script completed!")
