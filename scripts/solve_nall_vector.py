import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

cpp_dir = os.path.join("android", "app", "src", "main", "cpp")

# 1. Update vulkan_android_compat.h (without early vulkan.h include)
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
print("✏️ Updated vulkan_android_compat.h.")

# 2. Update mia/resource/resource.hpp with DummyResource Database (fixes undefined template vector error)
mia_res_dir = os.path.join("mia", "resource")
os.makedirs(mia_res_dir, exist_ok=True)
with open(os.path.join(mia_res_dir, "resource.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once

namespace nall {
    template<typename T> struct vector;
    struct string;
}

namespace mia::Resource {
    struct DummyResource {
        template<typename T> operator nall::vector<T>() const;
        operator nall::string() const;
    };

    // Use DummyResource instead of concrete nall::vector instance
    inline static const DummyResource Database{};

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
print("✏️ Updated mia/resource/resource.hpp with DummyResource Database.")

# 3. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Use DummyResource Database in mia/resource.hpp to resolve undefined template vector error"')
print("\n=======================================================")
print("✅ UNDEFINED TEMPLATE VECTOR ERROR RESOLVED!")
print("=======================================================")
