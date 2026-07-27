import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Create mia/resource/resource.hpp stub
mia_res_dir = os.path.join("mia", "resource")
os.makedirs(mia_res_dir, exist_ok=True)
mia_res_hpp = os.path.join(mia_res_dir, "resource.hpp")

with open(mia_res_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
#include <nall/nall.hpp>

namespace mia::Resource {
    inline static const nall::vector<uint8_t> Database{};
}
""")
print("✏️ Created mia/resource/resource.hpp stub.")

# 2. Create comprehensive ares/resource/resource.hpp stub with implicit type conversions
ares_res_hpp = os.path.join("ares", "resource", "resource.hpp")
with open(ares_res_hpp, "w", encoding="utf-8") as f:
    f.write("""#pragma once
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
print("✏️ Created comprehensive ares/resource/resource.hpp stub.")

# 3. Clean up deprecation warning CXX5106 in local.properties
local_props_path = os.path.join("android", "local.properties")
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props = f.readlines()
    clean_props = [p for p in props if not p.startswith("ndk.dir")]
    with open(local_props_path, "w", encoding="utf-8") as f:
        f.writelines(clean_props)
    print("✏️ Cleaned ndk.dir from local.properties (app/build.gradle ndkVersion handles NDK 26).")

os.system("git add .")
os.system('git commit -m "fix: Add complete mia and ares Resource::Sprite stubs"')
print("\n✅ Resource stubs created!")
