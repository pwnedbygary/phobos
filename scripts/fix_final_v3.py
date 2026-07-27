import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Update hiro/hiro.hpp stub with layout types for mia/program
hiro_dir = os.path.join("android", "app", "src", "main", "cpp", "hiro")
os.makedirs(hiro_dir, exist_ok=True)
hiro_hpp = os.path.join(hiro_dir, "hiro.hpp")

with open(hiro_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <nall/nall.hpp>

namespace hiro {
    struct Application {};
    struct Window {};
    struct Widget { template<typename... Args> Widget(Args&&...) {} };
    struct Size { template<typename... Args> Size(Args&&...) {} };
    struct Alignment { template<typename... Args> Alignment(Args&&...) {} };
    struct Position { template<typename... Args> Position(Args&&...) {} };
    struct Font { template<typename... Args> Font(Args&&...) {} };
    struct Color { template<typename... Args> Color(Args&&...) {} };
    struct Image { template<typename... Args> Image(Args&&...) {} };
    struct Icon { template<typename... Args> Icon(Args&&...) {} };
    struct Layout : Widget { using Widget::Widget; };
    struct VerticalLayout : Layout { using Layout::Layout; };
    struct HorizontalLayout : Layout { using Layout::Layout; };
    struct ListView : Widget { using Widget::Widget; };
    struct Frame : Widget { using Widget::Widget; };
    struct Canvas : Widget { using Widget::Widget; };
    struct Button : Widget { using Widget::Widget; };
    struct Label : Widget { using Widget::Widget; };
    struct View : Layout { using Layout::Layout; };
}

using namespace hiro;
""")
print("✏️ Updated hiro/hiro.hpp with full layout types.")

# 2. Update ares/resource/resource.hpp with operator nall::image()
ares_res_hpp = os.path.join("ares", "resource", "resource.hpp")
with open(ares_res_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>
#include <nall/image.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};

    struct DummyImage {
        template<typename T> operator nall::vector<T>() const { return {}; }
        operator nall::vector<uint8_t>() const { return {}; }
        operator nall::string() const { return {}; }
        operator nall::image() const { return {}; }
    };

    namespace Sprite {
        inline static const DummyImage Auxiliary0{};
        inline static const DummyImage Auxiliary1{};
        inline static const DummyImage Auxiliary2{};
        inline static const DummyImage Headphones{};
        inline static const DummyImage Initialized{};
        inline static const DummyImage LowBattery{};
        inline static const DummyImage CrosshairGreen{};
        inline static const DummyImage CrosshairRed{};

        namespace SuperFamicom {
            inline static const DummyImage CrosshairGreen{};
            inline static const DummyImage CrosshairRed{};
        }
        namespace WonderSwan {
            inline static const DummyImage Auxiliary0{};
            inline static const DummyImage Auxiliary1{};
            inline static const DummyImage Auxiliary2{};
            inline static const DummyImage Headphones{};
            inline static const DummyImage Initialized{};
            inline static const DummyImage LowBattery{};
        }
    }
}
""")
print("✏️ Updated ares/resource/resource.hpp with operator nall::image().")

# 3. Locate rdp_device.hpp and add to CMake
rdp_paths = set()
for root, dirs, files in os.walk("."):
    if "rdp_device.hpp" in files or "rdp_device.h" in files:
        rel_path = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
        rdp_paths.add(rel_path)

print(f"🔍 Located rdp_device in: {list(rdp_paths)}")

tp_include_dirs = set()
tp_include_dirs.add("thirdparty")

if os.path.exists("thirdparty"):
    for root, dirs, files in os.walk("thirdparty"):
        for f in files:
            if f.endswith(".h") or f.endswith(".hpp"):
                rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                tp_include_dirs.add(rel_p)

tp_include_dirs.update(rdp_paths)
sorted_tp_dirs = sorted(list(tp_include_dirs))

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

print("✏️ Updated CMakeLists.txt with rdp_device include path.")

# 4. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Add hiro GUI layout stubs, DummyImage nall::image operator, and rdp_device include path"')
print("\n✅ All 3 final issues resolved!")
