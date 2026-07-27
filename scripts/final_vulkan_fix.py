import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Clean context.hpp and place ONLY the 3 missing struct definitions right after vulkan_headers.hpp
context_hpp_path = os.path.join("ares", "n64", "vulkan", "parallel-rdp", "vulkan", "context.hpp")
if os.path.exists(context_hpp_path):
    with open(context_hpp_path, "r", encoding="utf-8") as f:
        c_txt = f.read()

    # Remove redundant typedef lines if present
    bad_prefix = """#pragma once
#include <cstdint>

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef enum VkStructureType VkStructureType;
"""
    c_txt = c_txt.replace(bad_prefix, "")
    c_txt = c_txt.replace("typedef uint32_t VkFlags;\n", "")
    c_txt = c_txt.replace("typedef uint32_t VkBool32;\n", "")
    c_txt = c_txt.replace("typedef enum VkStructureType VkStructureType;\n", "")

    clean_struct_block = """
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
        if '#include "vulkan_headers.hpp"' in c_txt:
            c_txt = c_txt.replace('#include "vulkan_headers.hpp"', '#include "vulkan_headers.hpp"\n' + clean_struct_block)
        else:
            c_txt = clean_struct_block + c_txt

    with open(context_hpp_path, "w", encoding="utf-8") as f:
        f.write(c_txt)
    print("✏️ Cleaned context.hpp (removed redundant typedefs).")

# Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Remove redundant Vulkan typedefs from context.hpp"')
print("\n=======================================================")
print("✅ FINAL VULKAN FIX COMPLETED!")
print("=======================================================")
