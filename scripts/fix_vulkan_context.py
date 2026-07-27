import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Patch ares/n64/vulkan/parallel-rdp/vulkan/context.hpp directly
context_hpp_path = os.path.join("ares", "n64", "vulkan", "parallel-rdp", "vulkan", "context.hpp")
if os.path.exists(context_hpp_path):
    with open(context_hpp_path, "r", encoding="utf-8") as f:
        c_txt = f.read()

    compat_block = """
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
            c_txt = c_txt.replace('#include "vulkan_headers.hpp"', '#include "vulkan_headers.hpp"\n' + compat_block)
        elif "#include <vulkan/vulkan.h>" in c_txt:
            c_txt = c_txt.replace("#include <vulkan/vulkan.h>", "#include <vulkan/vulkan.h>\n" + compat_block)
        else:
            c_txt = compat_block + c_txt

        with open(context_hpp_path, "w", encoding="utf-8") as f:
            f.write(c_txt)
        print("✏️ Patched context.hpp directly with Vulkan 1.3 extension structs.")

# 2. Remove force-include flag from CMakeLists.txt
cmake_path = "CMakeLists.txt"
if os.path.exists(cmake_path):
    with open(cmake_path, "r", encoding="utf-8") as f:
        cm_txt = f.read()

    # Remove -include flag
    lines = cm_txt.splitlines()
    clean_lines = [l for l in lines if "vulkan_android_compat.h" not in l]
    with open(cmake_path, "w", encoding="utf-8") as f:
        f.write("\n".join(clean_lines) + "\n")
    print("✏️ Removed force-include flag from CMakeLists.txt.")

# 3. Remove temporary vulkan_android_compat.h file if present
compat_file = os.path.join("android", "app", "src", "main", "cpp", "vulkan_android_compat.h")
if os.path.exists(compat_file):
    os.remove(compat_file)
    print("🧹 Removed temporary vulkan_android_compat.h.")

# 4. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Patch Vulkan 1.3 extension structs directly in context.hpp"')
print("\n✅ Script completed successfully!")
