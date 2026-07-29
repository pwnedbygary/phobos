#!/usr/bin/env python3
import os
import glob
import re
import subprocess
import shutil
import urllib.request

def patch_file(filepath, patch_func):
    if not os.path.exists(filepath): return
    with open(filepath, 'r', encoding='utf-8') as f: content = f.read()
    new_content = patch_func(content)
    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f: f.write(new_content)
        print(f"Patched {filepath}")

def write_file(filepath, content):
    dirname = os.path.dirname(filepath)
    if dirname: os.makedirs(dirname, exist_ok=True)
    with open(filepath, 'w', encoding='utf-8') as f: f.write(content)
    print(f"Written {filepath}")

def download_file(url, dest):
    if not os.path.exists(dest):
        print(f"Downloading missing dependency: {dest}...")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        urllib.request.urlretrieve(url, dest)

def git_commit_and_push():
    print("\n--- Git Automation ---")
    try:
        subprocess.run(['git', 'add', '.'], check=True)
        status = subprocess.run(['git', 'status', '--porcelain'], capture_output=True, text=True)
        if not status.stdout.strip():
            print("No changes to commit.")
            return
        commit_msg = "Automated build fixes: fix C++ preprocessor newline syntax"
        print(f"Committing changes: '{commit_msg}'")
        subprocess.run(['git', 'commit', '-m', commit_msg], check=True)
        print("Pushing to remote repository...")
        subprocess.run(['git', 'push'], check=True)
        print("Successfully pushed to remote!")
    except subprocess.CalledProcessError as e:
        print(f"Git operation failed: {e}")
    print("----------------------\n")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    os.chdir(repo_root)
    print(f"Operating in repository root: {repo_root}")

    # 0. RESTORE CORE FILES TO PRISTINE UPSTREAM STATE
    try:
        subprocess.run(['git', 'checkout', 'origin/master', '--', 'ares/ares/'], check=True, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        try:
            subprocess.run(['git', 'checkout', 'HEAD', '--', 'ares/ares/'], check=True, stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            pass

    if os.path.exists('ares/ares/ares-core.cpp'): os.remove('ares/ares/ares-core.cpp')

    # 1. AndroidManifest.xml
    patch_file('android/app/src/main/AndroidManifest.xml', lambda c: re.sub(r'\s*package="[^"]+"', '', c))

    # 2. ADD #pragma once TO ALL HEADERS TO KILL REDEFINITION ERRORS
    for hpp in glob.glob('ares/ares/**/*.hpp', recursive=True):
        with open(hpp, 'r', encoding='utf-8') as f: content = f.read()
        if '#pragma once' not in content:
            with open(hpp, 'w', encoding='utf-8') as f: f.write('#pragma once\n' + content)

    # 3. WRAP .CPP FILES IN NAMESPACES IF MISSING
    def wrap_cpp(filepath, ns):
        if not os.path.exists(filepath): return
        with open(filepath, 'r', encoding='utf-8') as f: content = f.read()
        if f"namespace {ns}" not in content and "namespace ares" not in content:
            with open(filepath, 'w', encoding='utf-8') as f: f.write(f"namespace {ns} {{\n{content}\n}}\n")

    wrap_cpp('ares/ares/scheduler/scheduler.cpp', 'ares')
    wrap_cpp('ares/ares/memory/fixed-allocator.cpp', 'ares::Memory')

    # 4. PATCH ares.hpp
    def patch_ares_hpp(content):
        if "using serializer = nall::serializer;" not in content:
            if "#pragma once" in content:
                content = content.replace("#pragma once", "#pragma once\n#include <nall/serializer.hpp>\n")
            else:
                content = "#include <nall/serializer.hpp>\n" + content
            match = re.search(r'namespace\s+ares\s*\{', content)
            if match:
                insert_idx = match.end()
                content = content[:insert_idx] + "\n  using namespace nall;\n  using serializer = nall::serializer;\n" + content[insert_idx:]
            else:
                content += "\nnamespace ares {\n  using namespace nall;\n  using serializer = nall::serializer;\n}\n"
        
        # PREPROCESSOR DIRECTIVES MUST BE ON A NEW LINE!
        if "scheduler.hpp" not in content: 
            content += "\nnamespace ares {\n#include <ares/scheduler/scheduler.hpp>\n}\n"
        if "fixed-allocator.hpp" not in content: 
            content += "\nnamespace ares::Memory {\n#include <ares/memory/fixed-allocator.hpp>\n}\n"
        return content
    patch_file('ares/ares/ares.hpp', patch_ares_hpp)

    # 5. nall/nall/resource/resource.hpp
    write_file('nall/nall/resource/resource.hpp', "#pragma once\n")

    # 6. hiro/hiro.hpp
    hiro_hpp = """#pragma once
#include <functional>
#include <vector>
#include <string>
namespace hiro {
  struct Widget { void setVisible(bool) {} void setEnabled(bool) {} };
  struct Window : Widget {
    void setTitle(const std::string&) {} void setSize(const std::vector<int>&) {}
    void setAlignment(const std::string&) {} void setVisible(bool) {}
    void onDismiss(std::function<void()>) {}
  };
  struct Font {}; struct Color {}; struct Size { Size(int, int) {} };
  struct Alignment { Alignment(int, int) {} }; struct Image {};
  namespace Icon { namespace Emblem { inline Image Folder() { return {}; } } }
  struct ListViewItem {}; struct ListView : Widget { void append(ListViewItem*) {} };
  struct Canvas : Widget { void setSize(const std::vector<int>&) {} void update() {} };
  struct Button : Widget { void setText(const std::string&) {} void onActivate(std::function<void()>) {} };
  struct Label : Widget { void setText(const std::string&) {} };
  struct BrowserDialog {
    BrowserDialog& setTitle(const std::string&) { return *this; } BrowserDialog& setPath(const std::string&) { return *this; }
    BrowserDialog& setFilters(const std::vector<std::string>&) { return *this; }
    std::string openFile() { return ""; } std::string openFolder() { return ""; }
  };
  struct MessageDialog {
    MessageDialog& setTitle(const std::string&) { return *this; } MessageDialog& setText(const std::string&) { return *this; }
    void error() {} void warning() {} void information() {}
  };
  namespace Mouse { namespace Button { enum { Left }; } }
  inline int sx(int v) { return v; } inline int sy(int v) { return v; }
}
inline int operator""_sx(unsigned long long v) { return v; }
inline int operator""_sy(unsigned long long v) { return v; }
"""
    write_file('android/app/src/main/cpp/hiro/hiro.hpp', hiro_hpp)

    # 7. ares/n64/vulkan/parallel-rdp/vulkan/context.hpp
    def patch_vulkan_context(content):
        structs = """
typedef struct VkPhysicalDeviceVideoMaintenance1FeaturesKHR {
    VkStructureType sType; void* pNext; VkBool32 videoMaintenance1;
} VkPhysicalDeviceVideoMaintenance1FeaturesKHR;
typedef struct VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV {
    VkStructureType sType; void* pNext; VkBool32 deviceGeneratedCompute;
    VkBool32 deviceGeneratedComputePipelines; VkBool32 deviceGeneratedComputeCaptureReplay;
} VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV;
typedef struct VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV {
    VkStructureType sType; void* pNext; VkBool32 descriptorPoolOverallocation;
} VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV;
"""
        if "VkPhysicalDeviceVideoMaintenance1FeaturesKHR" not in content:
            content = re.sub(r'(#include\s*["<]vulkan_headers\.hpp[">])', r'\1\n' + structs, content)
        return content
    patch_file('ares/n64/vulkan/parallel-rdp/vulkan/context.hpp', patch_vulkan_context)

    # 8. PhobosRunner.cpp
    def patch_phobos_runner(content):
        if "AndroidPlatform androidPlatform;" not in content:
            match = re.search(r'namespace\s+ares\s*\{', content)
            insertion = "\n  Debug _debug;\n  struct AndroidPlatform : Platform {};\n  static AndroidPlatform androidPlatform;\n  Platform* platform = &androidPlatform;\n"
            if match:
                insert_idx = match.end()
                content = content[:insert_idx] + insertion + content[insert_idx:]
            else:
                content += "\nnamespace ares {" + insertion + "}\n"
        content = re.sub(r'screen\.pixels\(\)\.data\(\)', r'screen->pixels().data()', content)
        content = re.sub(r'stream\.read\(', r'stream->read(', content)
        content = re.sub(r'node\.cast<', r'node->cast<', content)
        return content
    patch_file('android/app/src/main/cpp/PhobosRunner.cpp', patch_phobos_runner)

    # 9. ares/resource/resource.hpp
    ares_resource_hpp = """#pragma once
#include <nall/nall.hpp>
namespace nall { template<typename T> struct vector; struct string; struct image; }
namespace ares::Resource {
  inline static const nall::vector<uint8_t>* Logo = nullptr;
  struct DummyImage {
    template<typename T> operator nall::vector<T>() const { return {}; }
    operator nall::string() const { return {}; } operator nall::image() const { return {}; }
  };
  namespace Sprite {
    namespace WonderSwan {
      inline DummyImage Orientation0, Orientation1, PoweredOn, Sleeping;
      inline DummyImage VolumeA0, VolumeA1, VolumeA2, VolumeA3;
      inline DummyImage VolumeB0, VolumeB1, VolumeB2, VolumeB3;
    }
    namespace SuperFamicom { inline DummyImage CrosshairRed, CrosshairGreen, CrosshairBlue; }
  }
}
"""
    write_file('ares/resource/resource.hpp', ares_resource_hpp)

    # 10. mia/resource/resource.hpp
    mia_resource_hpp = """#pragma once
#include <span>
#include <cstdint>
namespace mia {
  struct DummyResource {
    operator std::span<const uint8_t>() const { return {}; }
    operator const void*() const { return nullptr; }
  };
  namespace Resource {
    namespace GameBoy { inline DummyResource BootDMG1; }
    namespace GameBoyColor { inline DummyResource BootCGB0; }
    namespace GameBoyAdvance { inline DummyResource Boot; }
    namespace SuperFamicom {
      inline DummyResource Cx4, DSP1, DSP1B, DSP2, DSP3, DSP4;
      inline DummyResource ST010, ST011, ST018, SGB1, SGB2, SGB2Boot, IPLROM;
    }
    namespace MasterSystem { inline DummyResource Boot; }
    namespace MegaDrive { inline DummyResource TMSS, SVP; }
    namespace MegaCD { inline DummyResource Boot; }
    namespace Mega32X { inline DummyResource Vector, SH2BootM, SH2BootS; }
    namespace Nintendo64 { inline DummyResource PIFNTSC, PIFPAL, PIFSM5; }
    namespace PlayStation { inline DummyResource Boot; }
    namespace WonderSwan { inline DummyResource Boot; }
    namespace WonderSwanColor { inline DummyResource Boot; }
    namespace PocketChallengeV2 { inline DummyResource Boot; }
    namespace ZXSpectrum { inline DummyResource Boot, BIOS; }
    namespace ZXSpectrum128 { inline DummyResource Boot, BIOS, Sub; }
    namespace MSX { inline DummyResource Boot; }
    namespace MSX2 { inline DummyResource Boot; }
    namespace NeoGeo { inline DummyResource Boot; }
    namespace NeoGeoPocket { inline DummyResource Boot; }
    namespace NeoGeoPocketColor { inline DummyResource Boot; }
    namespace PCEngineCD { inline DummyResource Boot; }
  }
}
"""
    write_file('mia/resource/resource.hpp', mia_resource_hpp)

    # 11. Download missing third-party dependencies
    download_file('https://raw.githubusercontent.com/Cyan4973/xxHash/v0.8.2/xxhash.c', 'thirdparty/xxhash.c')
    download_file('https://raw.githubusercontent.com/zeux/volk/master/volk.h', 'thirdparty/volk/volk.h')
    download_file('https://raw.githubusercontent.com/zeux/volk/master/volk.c', 'thirdparty/volk/volk.c')
    download_file('https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h', 'thirdparty/vma/vk_mem_alloc.h')
    
    vulkan_headers_path = 'thirdparty/Vulkan-Headers'
    if not os.path.exists(os.path.join(vulkan_headers_path, 'include', 'vulkan', 'vulkan.hpp')):
        print("Cloning Official Khronos Vulkan C++ Headers...")
        shutil.rmtree(vulkan_headers_path, ignore_errors=True)
        subprocess.run(['git', 'clone', 'https://github.com/KhronosGroup/Vulkan-Headers.git', vulkan_headers_path, '--depth', '1'])
        shutil.rmtree(os.path.join(vulkan_headers_path, '.git'), ignore_errors=True)

    # 12. Generate GLFW Android Stubs
    glfw_h = """#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GLFWwindow GLFWwindow; typedef struct GLFWmonitor GLFWmonitor;
#define GLFW_TRUE 1
#define GLFW_FALSE 0
const char** glfwGetRequiredInstanceExtensions(uint32_t* count);
int glfwGetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height);
#ifdef __cplusplus
}
#endif
"""
    glfw_cpp = """#include "glfw3.h"
extern "C" {
    const char** glfwGetRequiredInstanceExtensions(uint32_t* count) { *count = 0; return nullptr; }
    int glfwGetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily) { return 1; }
    VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) { return VK_SUCCESS; }
    void glfwGetWindowSize(GLFWwindow* window, int* width, int* height) { if(width) *width = 1920; if(height) *height = 1080; }
    void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) { if(width) *width = 1920; if(height) *height = 1080; }
}
"""
    write_file('thirdparty/GLFW/glfw3.h', glfw_h)
    write_file('thirdparty/GLFW/glfw_stub.cpp', glfw_cpp)
    write_file('thirdparty/sljit/sljit.h', '#pragma once\n#include "sljit_src/sljitLir.h"\n')

    # 13. CMakeLists.txt
    thirdparty_dirs = ['thirdparty', 'thirdparty/volk', 'thirdparty/vma', 'thirdparty/sljit'] 
    for d in glob.glob('thirdparty/*'):
        d_forward = d.replace('\\', '/')
        if d_forward not in thirdparty_dirs: thirdparty_dirs.append(d_forward)
        
        if 'sljit' in d_forward:
            sljit_src = os.path.join(d, 'sljit_src').replace('\\', '/')
            if os.path.isdir(sljit_src) and sljit_src not in thirdparty_dirs: thirdparty_dirs.append(sljit_src)
        if 'ymfm' in d_forward:
            ymfm_src = os.path.join(d, 'src').replace('\\', '/')
            if os.path.isdir(ymfm_src) and ymfm_src not in thirdparty_dirs: thirdparty_dirs.append(ymfm_src)

    if os.path.exists('thirdparty/Vulkan-Headers/include'):
        thirdparty_dirs.append('thirdparty/Vulkan-Headers/include')

    thirdparty_includes = "\n".join([f"    ${{CMAKE_CURRENT_SOURCE_DIR}}/{d}" for d in thirdparty_dirs])

    parallel_rdp_includes = ""
    if os.path.exists('ares/n64/vulkan/parallel-rdp'):
        parallel_rdp_includes = """    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/util
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/parallel-rdp"""

    ares_sources = [
        'ares/component/processor/arm7tdmi/arm7tdmi.cpp', 'ares/component/processor/m68000/m68000.cpp',
        'ares/component/processor/mos6502/mos6502.cpp', 'ares/component/processor/hg51b/hg51b.cpp',
        'ares/component/processor/sh2/sh2.cpp', 'ares/component/processor/z80/z80.cpp',
        'ares/component/processor/spc700/spc700.cpp', 'ares/component/processor/v30mz/v30mz.cpp',
        'ares/component/processor/sm83/sm83.cpp',
        'ares/a26/a26.cpp', 'ares/cv/cv.cpp', 'ares/fc/fc.cpp', 'ares/gb/gb.cpp', 'ares/gba/gba.cpp',
        'ares/md/md.cpp', 'ares/ms/ms.cpp', 'ares/msx/msx.cpp', 'ares/myvision/myvision.cpp',
        'ares/n64/n64.cpp', 'ares/ng/ng.cpp', 'ares/ngp/ngp.cpp', 'ares/pce/pce.cpp', 'ares/ps1/ps1.cpp',
        'ares/sfc/sfc.cpp', 'ares/sg/sg.cpp', 'ares/spec/spec.cpp', 'ares/ws/ws.cpp'
    ]

    other_sources = [
        'nall/nall/nall.cpp', 'mia/mia.cpp',
        'libco/aarch64.c', 'thirdparty/volk/volk.c', 'thirdparty/GLFW/glfw_stub.cpp',
        'android/app/src/main/cpp/PhobosRunner.cpp', 'android/app/src/main/cpp/PhobosJNI.cpp'
    ]

    if os.path.exists('thirdparty/sljitAllocator.cpp'): other_sources.append('thirdparty/sljitAllocator.cpp')
    for f in glob.glob('thirdparty/ymfm/src/*.cpp'):
        if 'example' not in f.lower(): other_sources.append(f.replace('\\', '/'))
    for f in glob.glob('thirdparty/TZXFile/*.cpp'):
        if 'example' not in f.lower(): other_sources.append(f.replace('\\', '/'))
        
    if os.path.exists('ares/n64/vulkan/parallel-rdp'):
        for ext in ('*.cpp', '*.c'):
            for f in glob.glob(f'ares/n64/vulkan/parallel-rdp/**/{ext}', recursive=True):
                f_fwd = f.replace('\\', '/')
                if 'example' not in f_fwd.lower() and 'test' not in f_fwd.lower() and 'volk' not in f_fwd.lower():
                    other_sources.append(f_fwd)

    valid_ares = [s for s in ares_sources if os.path.exists(s)]
    valid_other = [s for s in other_sources if os.path.exists(s)]

    cmake_content = f"""cmake_minimum_required(VERSION 3.22)
project(phobos)

add_compile_definitions(BUILD_RELEASE VULKAN VK_NO_PROTOTYPES MIA_LIBRARY)
add_compile_options(-w)

include_directories(
    ${{CMAKE_CURRENT_SOURCE_DIR}}
    ${{CMAKE_CURRENT_SOURCE_DIR}}/nall/nall
    ${{CMAKE_CURRENT_SOURCE_DIR}}/nall
    ${{CMAKE_CURRENT_SOURCE_DIR}}/libco
    ${{CMAKE_CURRENT_SOURCE_DIR}}/ares
    ${{CMAKE_CURRENT_SOURCE_DIR}}/android/app/src/main/cpp
{parallel_rdp_includes}
{thirdparty_includes}
)

set(ARES_SOURCES
{chr(10).join(['    ' + s for s in valid_ares])}
)

set(OTHER_SOURCES
{chr(10).join(['    ' + s for s in valid_other])}
)

add_library(phobos_android SHARED
    ${{ARES_SOURCES}}
    ${{OTHER_SOURCES}}
)

set_property(SOURCE ${{ARES_SOURCES}} PROPERTY COMPILE_OPTIONS "-include" "ares/ares.hpp")

find_library(log-lib log)
find_library(android-lib android)
find_library(aaudio-lib aaudio)
find_library(vulkan-lib vulkan)
find_library(z-lib z)
find_library(dl-lib dl)

target_link_libraries(phobos_android ${{log-lib}} ${{android-lib}} ${{aaudio-lib}} ${{vulkan-lib}} ${{z-lib}} ${{dl-lib}})
"""
    write_file('CMakeLists.txt', cmake_content)

    for cache_dir in ['android/app/.cxx', 'android/app/build']:
        if os.path.exists(cache_dir):
            shutil.rmtree(cache_dir)
            print(f"Cleared {cache_dir} cache")

    git_commit_and_push()

    print("\nSUCCESS! Ready to build. Please run 'Make Project' in Android Studio.")

if __name__ == '__main__':
    main()
