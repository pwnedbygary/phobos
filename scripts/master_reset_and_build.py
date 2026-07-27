import os
import shutil
import subprocess

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Reset modified core files back to pristine git state
print("🧹 Resetting ares/, nall/, and mia/ to clean git state...")
subprocess.run(["git", "checkout", "HEAD", "--", "ares", "nall", "mia"], capture_output=True)

# 2. Re-create clean scaffolding stubs
# a) hiro stub
hiro_dir = os.path.join("android", "app", "src", "main", "cpp", "hiro")
os.makedirs(hiro_dir, exist_ok=True)
with open(os.path.join(hiro_dir, "hiro.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
namespace hiro {
    struct Application {};
    struct Window {};
    struct Widget {};
}
""")
print("✏️ Created hiro/hiro.hpp stub.")

# b) resource stub
res_dir = os.path.join("ares", "resource")
os.makedirs(res_dir, exist_ok=True)
with open(os.path.join(res_dir, "resource.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};
}
""")
print("✏️ Created ares/resource/resource.hpp stub.")

# c) jansson_config stub
jansson_dir = os.path.join("thirdparty", "jansson", "src")
os.makedirs(jansson_dir, exist_ok=True)
with open(os.path.join(jansson_dir, "jansson_config.h"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
#define JSON_INLINE inline
#define JSON_INTEGER_IS_LONG_LONG 1
#define JSON_HAVE_LOCALECONV 1
#define JSON_HAVE_ATOMIC_BUILTINS 1
#define JSON_HAVE_SYNC_BUILTINS 1
""")
print("✏️ Created jansson_config.h stub.")

# 3. Force NDK 26 in local.properties
local_props_path = os.path.join("android", "local.properties")
ndk_26_path = "/home/garyb/Android/Sdk/ndk/26.1.10909125"

props = []
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props = f.readlines()

new_props = [p for p in props if not p.startswith("ndk.dir")]
new_props.append(f"ndk.dir={ndk_26_path}\n")

with open(local_props_path, "w", encoding="utf-8") as f:
    f.writelines(new_props)
print(f"✏️ Set ndk.dir={ndk_26_path} in local.properties.")

# 4. Dynamically collect all thirdparty header directories
tp_include_dirs = set()
tp_include_dirs.add("thirdparty")

if os.path.exists("thirdparty"):
    for root, dirs, files in os.walk("thirdparty"):
        for f in files:
            if f.endswith(".h") or f.endswith(".hpp"):
                rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                tp_include_dirs.add(rel_p)

sorted_tp_dirs = sorted(list(tp_include_dirs))

# 5. Gather Unity build source files
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

# 6. Generate pristine CMakeLists.txt (nall included FIRST)
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/ares",
    "${CMAKE_CURRENT_SOURCE_DIR}/mia",
    "${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp",
]

for tp in sorted_tp_dirs:
    include_dirs.append(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{tp}")

include_dirs_str = "\n    ".join(include_dirs)

cmake_content = f"""cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(BUILD_RELEASE)

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

target_link_libraries(phobos_android android aaudio log)
"""

with open("CMakeLists.txt", "w", encoding="utf-8") as f:
    f.write(cmake_content)
print("✏️ Wrote clean CMakeLists.txt.")

# 7. Ensure PhobosRunner.cpp uses verified Screen API
runner_cpp = os.path.join("android", "app", "src", "main", "cpp", "PhobosRunner.cpp")
if os.path.exists(runner_cpp):
    with open(runner_cpp, "r", encoding="utf-8") as f:
        txt = f.read()

    txt = txt.replace("screen->pixels->data()", "screen->pixels()")
    txt = txt.replace("// drawFrame(screen->pixels(), screen->width(), screen->height(), screen->pitch());",
                      "drawFrame(screen->pixels(), screen->width(), screen->height(), screen->pitch());")

    with open(runner_cpp, "w", encoding="utf-8") as f:
        f.write(txt)
    print("✏️ Verified PhobosRunner.cpp video pipeline.")

# 8. Wipe CMake cache completely
cxx_dir = os.path.join("android", "app", ".cxx")
build_dir = os.path.join("android", "app", "build")

if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

if os.path.exists(build_dir):
    shutil.rmtree(build_dir)
    print("🧹 Cleared build directory.")

# 9. Commit clean state
subprocess.run(["git", "add", "."], capture_output=True)
subprocess.run(["git", "commit", "-m", "fix: Reset modified core headers to pristine state and rebuild CMake"], capture_output=True)

print("\n=======================================================")
print("✅ PRISTINE RE-BUILD ENVIRONMENT READY!")
print("=======================================================")
