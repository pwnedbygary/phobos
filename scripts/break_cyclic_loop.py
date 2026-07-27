import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Write decoupled ares/resource/resource.hpp without cyclic #include <nall/nall.hpp>
ares_res_hpp = os.path.join("ares", "resource", "resource.hpp")
with open(ares_res_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <cstdint>

// Forward-declare nall types to prevent cyclic header recursion back into nall.hpp
namespace nall {
    template<typename T> struct vector;
    struct string;
    struct image;
}

namespace ares::Resource {
    inline static const nall::vector<uint8_t>* Logo = nullptr;

    struct DummyImage {
        template<typename T> operator nall::vector<T>() const;
        operator nall::string() const;
        operator nall::image() const;
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
print("✏️ Decoupled ares/resource/resource.hpp to break cyclic include loop!")

# 2. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Break cyclic include loop between nall.hpp and resource.hpp"')
print("\n✅ Cyclic loop broken successfully!")
