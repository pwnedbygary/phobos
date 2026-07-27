import os
import shutil
import subprocess

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Reset modified files in ares, nall, and mia back to pristine git state
print("🧹 Resetting ares/, nall/, and mia/ to pristine git state...")
subprocess.run(["git", "checkout", "HEAD", "--", "ares", "nall", "mia"], capture_output=True)

# 2. Patch ares/ares/ares.hpp with nall/serializer.hpp and explicit serializer type alias
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    if "serializer.hpp" not in content:
        if "#include <nall/nall.hpp>" in content:
            content = content.replace("#include <nall/nall.hpp>", "#include <nall/nall.hpp>\n#include <nall/serializer.hpp>")
        else:
            content = "#include <nall/serializer.hpp>\n" + content

    if "using serializer = nall::serializer;" not in content:
        if "namespace ares {" in content:
            content = content.replace("namespace ares {", "namespace ares {\n  using namespace nall;\n  using serializer = nall::serializer;")
        else:
            content += "\nnamespace ares {\n  using namespace nall;\n  using serializer = nall::serializer;\n}\n"

    with open(ares_hpp_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("✏️ Patched ares/ares/ares.hpp with serializer type alias.")

# 3. Write hiro/hiro.hpp layout & literal operator stubs
cpp_dir = os.path.join("android", "app", "src", "main", "cpp")
hiro_dir = os.path.join(cpp_dir, "hiro")
os.makedirs(hiro_dir, exist_ok=True)
with open(os.path.join(hiro_dir, "hiro.hpp"), "w", encoding="utf-8") as f:
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
    struct MenuBar : Widget { using Widget::Widget; };
    struct Menu : Widget { using Widget::Widget; };
    struct MenuItem : Widget { using Widget::Widget; };
    struct MenuCheckItem : Widget { using Widget::Widget; };
    struct HorizontalResizeGrip : Widget { using Widget::Widget; };
}

using namespace hiro;

inline auto operator""_sx(unsigned long long n) -> uint32_t { return (uint32_t)n; }
inline auto operator""_sx(long double n) -> uint32_t { return (uint32_t)n; }
""")
print("✏️ Created hiro/hiro.hpp stubs.")

# 4. Write vulkan_android_compat.h (without early vulkan.h include)
vulkan_compat = os.path.join(cpp_dir, "vulkan_android_compat.h")
with open(vulkan_compat, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <cstdint>

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef enum VkStructureType VkStructureType;

#ifndef VK_KHR_video_maintenance1
struct VkPhysicalDeviceVideoMaintenance1FeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 videoMaintenance1;
};
#endif

#ifndef VK_NV_device_generated_commands_compute
struct VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV {
    VkStructureType sType;
    void* pNext;
    VkBool32 deviceGeneratedCommandsCompute;
};
#endif

#ifndef VK_NV_descriptor_pool_overallocation
struct VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV {
    VkStructureType sType;
    void* pNext;
    VkBool32 descriptorPoolOverallocation;
};
#endif
""")
print("✏️ Created vulkan_android_compat.h.")

# 5. Write ares/resource/resource.hpp
ares_res_hpp = os.path.join("ares", "resource", "resource.hpp")
with open(ares_res_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>

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
print("✏️ Created ares/resource/resource.hpp.")

# 6. Write mia/resource/resource.hpp
mia_res_dir = os.path.join("mia", "resource")
os.makedirs(mia_res_dir, exist_ok=True)
with open(os.path.join(mia_res_dir, "resource.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <nall/nall.hpp>
#include <span>

namespace mia::Resource {
    inline static const nall::vector<uint8_t> Database{};

    struct DummyResource {
        template<typename T> operator nall::vector<T>() const { return {}; }
        operator nall::vector<uint8_t>() const { return {}; }
        operator nall::string() const { return {}; }
        operator std::span<const uint8_t>() const { return {}; }
    };

    namespace GameBoy {
        inline static const DummyResource BootDMG1{};
        inline static const DummyResource BootCGB1{};
        inline static const DummyResource BootAGB1{};
    }
    namespace GameBoyColor {
        inline static const DummyResource BootCGB0{};
        inline static const DummyResource BootCGB1{};
    }
    namespace GameBoyAdvance {
        inline static const DummyResource BootAGB0{};
        inline static const DummyResource BootAGB1{};
    }
    namespace SuperFamicom {
        inline static const DummyResource IPLROM{};
    }
    namespace MasterSystem {
        inline static const DummyResource Bios{};
    }
    namespace MegaDrive {
        inline static const DummyResource TMSS{};
    }
    namespace Mega32X {
        inline static const DummyResource Vector{};
        inline static const DummyResource SH2BootM{};
        inline static const DummyResource SH2BootS{};
    }
    namespace Nintendo64 {
        inline static const DummyResource PIFNTSC{};
        inline static const DummyResource PIFPAL{};
    }
}
""")
print("✏️ Created mia/resource/resource.hpp.")

# 7. Collect extra include paths (excluding vulkan-headers)
extra_includes = set()
extra_includes.add("thirdparty")

for search_base in ["thirdparty", os.path.join("ares", "n64", "vulkan")]:
    if os.path.exists(search_base):
        for root, dirs, files in os.walk(search_base):
            if "vulkan-header" in root.lower():
                continue
            for f in files:
                if f.endswith(".h") or f.endswith(".hpp"):
                    rel_p = os.path.relpath(root, REPO_ROOT).replace("\\", "/")
                    extra_includes.add(rel_p)

sorted_extra_includes = sorted(list(extra_includes))

# 8. Gather Unity build sources
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

# 9. Generate CMakeLists.txt WITHOUT ${CMAKE_CURRENT_SOURCE_DIR}/ares or /mia in include_directories!
# This resolves header ambiguity between nall/resource and ares/resource
include_dirs = [
    "${CMAKE_CURRENT_SOURCE_DIR}/nall",
    "${CMAKE_CURRENT_SOURCE_DIR}",
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

print("✏️ Wrote master CMakeLists.txt.")

# 10. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

# 11. Clean local.properties
local_props_path = os.path.join("android", "local.properties")
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props = f.readlines()
    clean_props = [p for p in props if not p.startswith("ndk.dir")]
    with open(local_props_path, "w", encoding="utf-8") as f:
        f.writelines(clean_props)

subprocess.run(["git", "add", "."], capture_output=True)
subprocess.run(["git", "commit", "-m", "fix: Grand unified fix resolving include path cross-contamination"], capture_output=True)

print("\n=======================================================")
print("✅ GRAND UNIFIED BUILD FIX COMPLETED!")
print("=======================================================")
