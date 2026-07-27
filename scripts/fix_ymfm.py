import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Search the ENTIRE repository for ymfm_opn.h to get its exact folder
ymfm_paths = set()
for root, dirs, files in os.walk("."):
    if "ymfm_opn.h" in files:
        rel_path = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
        ymfm_paths.add(rel_path)

print(f"🔍 Located ymfm_opn.h in: {list(ymfm_paths)}")

# 2. Recursively find ALL thirdparty directories containing headers
tp_include_dirs = set()
tp_include_dirs.add("thirdparty")

if os.path.exists("thirdparty"):
    for root, dirs, files in os.walk("thirdparty"):
        for f in files:
            if f.endswith(".h") or f.endswith(".hpp"):
                rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                tp_include_dirs.add(rel_p)

# Merge the exact location of ymfm_opn.h
tp_include_dirs.update(ymfm_paths)

sorted_tp_dirs = sorted(list(tp_include_dirs))
print("📦 All Include Directories configured for CMake:")
for d in sorted_tp_dirs:
    print(f"  - {d}")

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

sources_str = "\n    ".join(sources)

# 4. Generate CMake include paths
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/ares",
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
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

print("✏️ Updated CMakeLists.txt with explicit ymfm path.")

# 5. Clear CXX cache so Android Studio re-indexes the IDE header paths
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake CXX cache.")

# 6. Git Commit
os.system("git add .")
os.system('git commit -m "fix: Locate ymfm_opn.h and update CMake include paths"')
print("\n✅ Script completed successfully!")
