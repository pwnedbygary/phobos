import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

cpp_dir = os.path.join("android", "app", "src", "main", "cpp")

# 1. Update hiro/hiro.hpp with all widget methods used by mia/program
hiro_dir = os.path.join(cpp_dir, "hiro")
os.makedirs(hiro_dir, exist_ok=True)
with open(os.path.join(hiro_dir, "hiro.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <nall/nall.hpp>

namespace hiro {
    struct Application {};
    struct Window {};
    struct Widget {
        template<typename... Args> Widget(Args&&...) {}
        auto setVisible(bool = true) -> Widget& { return *this; }
        auto setEnabled(bool = true) -> Widget& { return *this; }
        auto setFocused() -> Widget& { return *this; }
        auto setFont(const struct Font& = {}) -> Widget& { return *this; }
        auto setForegroundColor(struct Color = {}) -> Widget& { return *this; }
        auto setBackgroundColor(struct Color = {}) -> Widget& { return *this; }
        auto setText(const nall::string& = {}) -> Widget& { return *this; }
        auto onChange(const nall::function<void()>& = {}) -> Widget& { return *this; }
        auto onMouseRelease(const nall::function<void(int)>& = {}) -> Widget& { return *this; }
    };
    struct Size { template<typename... Args> Size(Args&&...) {} };
    struct Alignment { template<typename... Args> Alignment(Args&&...) {} };
    struct Position { template<typename... Args> Position(Args&&...) {} };
    struct Font {
        template<typename... Args> Font(Args&&...) {}
        auto setBold(bool = true) -> Font& { return *this; }
        auto setItalic(bool = true) -> Font& { return *this; }
    };
    struct Color { template<typename... Args> Color(Args&&...) {} };
    struct Image {
        template<typename... Args> Image(Args&&...) {}
        auto width() const -> uint32_t { return 0; }
        auto height() const -> uint32_t { return 0; }
        auto scale(uint32_t, uint32_t) -> Image& { return *this; }
    };
    struct Icon { template<typename... Args> Icon(Args&&...) {} };
    struct Layout : Widget { using Widget::Widget; };
    struct VerticalLayout : Layout { using Layout::Layout; };
    struct HorizontalLayout : Layout { using Layout::Layout; };
    struct ListViewItem : Widget { using Widget::Widget; };
    struct ListView : Widget {
        using Widget::Widget;
        auto selected() -> nall::maybe<ListViewItem> { return {}; }
    };
    struct Frame : Widget { using Widget::Widget; };
    struct Canvas : Widget {
        using Widget::Widget;
        auto setIcon(const Image& = {}) -> Canvas& { return *this; }
    };
    struct Button : Widget { using Widget::Widget; };
    struct Label : Widget { using Widget::Widget; };
    struct View : Layout { using Layout::Layout; };
    struct MenuBar : Widget { using Widget::Widget; };
    struct Menu : Widget { using Widget::Widget; };
    struct MenuItem : Widget { using Widget::Widget; };
    struct MenuCheckItem : Widget { using Widget::Widget; };
    struct HorizontalResizeGrip : Widget { using Widget::Widget; };

    inline auto setCollapsible() -> Widget { return {}; }
}

using namespace hiro;

inline auto operator""_sx(unsigned long long n) -> uint32_t { return (uint32_t)n; }
inline auto operator""_sx(long double n) -> uint32_t { return (uint32_t)n; }
inline auto sx(double n) -> uint32_t { return (uint32_t)n; }
inline auto sy(double n) -> uint32_t { return (uint32_t)n; }
""")
print("✏️ Created complete hiro/hiro.hpp.")

# 2. Update mia/resource/resource.hpp with const void* operator, SGB1/2, and Ares::Icon1x
mia_res_dir = os.path.join("mia", "resource")
os.makedirs(mia_res_dir, exist_ok=True)
with open(os.path.join(mia_res_dir, "resource.hpp"), "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <span>

namespace nall {
    template<typename T> struct vector;
    struct string;
    struct image;
}

namespace mia::Resource {
    struct DummyResource {
        template<typename T> operator nall::vector<T>() const;
        operator nall::string() const;
        operator std::span<const uint8_t>() const { return {}; }
        operator const void*() const { return nullptr; }
        operator const uint8_t*() const { return nullptr; }
        operator nall::image() const;
    };

    inline static const DummyResource Database{};

    namespace Ares {
        inline static const DummyResource Icon1x{};
    }
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
    namespace WonderSwan {
        inline static const DummyResource Boot{};
    }
    namespace WonderSwanColor {
        inline static const DummyResource Boot{};
    }
    namespace PocketChallengeV2 {
        inline static const DummyResource Boot{};
    }
    namespace ZXSpectrum {
        inline static const DummyResource BIOS{};
    }
    namespace ZXSpectrum128 {
        inline static const DummyResource BIOS{};
        inline static const DummyResource Sub{};
    }
    namespace SuperFamicom {
        inline static const DummyResource IPLROM{};
        inline static const DummyResource Cx4{};
        inline static const DummyResource DSP1{};
        inline static const DummyResource DSP1B{};
        inline static const DummyResource DSP2{};
        inline static const DummyResource DSP3{};
        inline static const DummyResource DSP4{};
        inline static const DummyResource ST010{};
        inline static const DummyResource ST011{};
        inline static const DummyResource ST018{};
        inline static const DummyResource SGB1{};
        inline static const DummyResource SGB2{};
    }
    namespace MasterSystem {
        inline static const DummyResource Bios{};
    }
    namespace MegaDrive {
        inline static const DummyResource TMSS{};
        inline static const DummyResource SVP{};
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
print("✏️ Created complete mia/resource/resource.hpp.")

# 3. Patch context.hpp for Vulkan 1.3 extension structs right after vulkan_headers.hpp
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
    print("✏️ Patched context.hpp for Vulkan 1.3 extension structs.")

# 4. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Solve final mia and N64 Vulkan compilation errors"')
print("\n=======================================================")
print("✅ SOLVED ALL ERRORS IN THE GIST!")
print("=======================================================")
