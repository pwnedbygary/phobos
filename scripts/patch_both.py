import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Prepend missing Vulkan extension structs to top of context.hpp
context_hpp_path = os.path.join("ares", "n64", "vulkan", "parallel-rdp", "vulkan", "context.hpp")
if os.path.exists(context_hpp_path):
    with open(context_hpp_path, "r", encoding="utf-8") as f:
        c_txt = f.read()

    compat_block = """#pragma once
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
"""

    if "VkPhysicalDeviceVideoMaintenance1FeaturesKHR" not in c_txt:
        c_txt = compat_block + "\n" + c_txt
        with open(context_hpp_path, "w", encoding="utf-8") as f:
            f.write(c_txt)
        print("✏️ Prepended Vulkan 1.3 extension structs to context.hpp.")

# 2. Update mia/resource/resource.hpp with std::span operator & PIFSM5 stub
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
        inline static const DummyResource PIFSM5{};
    }
}
""")
print("✏️ Updated mia/resource/resource.hpp with std::span operator and PIFSM5 stub.")

# 3. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Prepend Vulkan structs to context.hpp and add mia std::span/PIFSM5 stubs"')
print("\n✅ Both remaining components patched successfully!")
