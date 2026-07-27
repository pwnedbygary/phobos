import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Update resource.hpp to include master <nall/nall.hpp>
res_hpp = os.path.join("ares", "resource", "resource.hpp")
if os.path.exists(res_hpp):
    with open(res_hpp, "w", encoding="utf-8") as f:
        f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};

    struct DummyImage {
        template<typename T> operator nall::vector<T>() const { return {}; }
        operator nall::vector<uint8_t>() const { return {}; }
        operator nall::string() const { return {}; }
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
    print("✏️ Updated ares/resource/resource.hpp with <nall/nall.hpp>.")

# 2. Inspect ares/n64/n64.hpp and system.cpp
n64_hpp = os.path.join("ares", "n64", "n64.hpp")
if os.path.exists(n64_hpp):
    print("\n=== ares/n64/n64.hpp (vulkan references) ===")
    with open(n64_hpp, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if "vulkan" in line.lower():
                print(f"  {line.strip()}")

n64_sys = os.path.join("ares", "n64", "system", "system.cpp")
if os.path.exists(n64_sys):
    print("\n=== ares/n64/system/system.cpp lines 75-95 ===")
    with open(n64_sys, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines[74:95], start=75):
            print(f"{i}: {line}", end="")
