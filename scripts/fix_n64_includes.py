import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Recursively find ALL directories under ares/n64/vulkan containing header files
n64_vulkan_dirs = set()
n64_vulkan_root = os.path.join("ares", "n64", "vulkan")

if os.path.exists(n64_vulkan_root):
    for root, dirs, files in os.walk(n64_vulkan_root):
        for f in files:
            if f.endswith(".h") or f.endswith(".hpp"):
                rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                n64_vulkan_dirs.add(rel_p)

print("📦 Found N64 Vulkan Include Directories:")
for d in sorted(list(n64_vulkan_dirs)):
    print(f"  - {d}")

# 2. Collect thirdparty directories
tp_include_dirs = set()
tp_include_dirs.add("thirdparty")

if os.path.exists("thirdparty"):
    for root, dirs, files in os.walk("thirdparty"):
        for f in files:
            if f.endswith(".h") or f.endswith(".hpp"):
                rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                tp_include_dirs.add(rel_p)

# Merge N64 Vulkan dirs into extra includes
all_extra_includes = sorted(list(tp_include_dirs.union(n64_vulkan_dirs)))

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

ymfm_cpp = os.path.join("thirdparty", "ymfm", "ymfm.cpp")
if os.path.exists(ymfm_cpp):
    sources.append(ymfm_cpp.replace("\\", "/"))

sources_str = "\n    ".join(sources)

# 4. Generate CMakeLists.txt
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/ares",
    "${CMAKE_CURRENT_SOURCE_DIR}/mia",
    "${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp",
]

for inc in all_extra_includes:
    include_dirs.append(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{inc}")

include_dirs_str = "\n    ".join(include_dirs)

cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(BUILD_RELEASE VULKAN)

add_compile_options(
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

print("✏️ Updated CMakeLists.txt with N64 parallel-rdp header directories.")

# 5. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Add N64 parallel-rdp header directories to CMakeLists.txt"')
print("\n✅ Fix script completed!")
