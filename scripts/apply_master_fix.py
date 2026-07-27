import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Fix ares/ares/ares.hpp: Bring nall namespace + serializer into namespace ares
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Ensure #include <nall/serializer.hpp> is included
    if "serializer.hpp" not in content:
        if "#include <nall/nall.hpp>" in content:
            content = content.replace("#include <nall/nall.hpp>", "#include <nall/nall.hpp>\n#include <nall/serializer.hpp>")
        else:
            content = "#include <nall/serializer.hpp>\n" + content

    # Ensure using namespace nall; is inside namespace ares
    if "using namespace nall;" not in content:
        if "namespace ares {" in content:
            content = content.replace("namespace ares {", "namespace ares {\n  using namespace nall;")

    with open(ares_hpp_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("✏️ Applied master patch to ares/ares/ares.hpp (nall::serializer + using namespace nall).")

# 2. Fix ares/resource/resource.hpp
res_hpp_path = os.path.join("ares", "resource", "resource.hpp")
if os.path.exists(res_hpp_path):
    with open(res_hpp_path, "w", encoding="utf-8") as f:
        f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>
#include <nall/vector.hpp>
#include <nall/string.hpp>

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
    print("✏️ Applied master patch to ares/resource/resource.hpp.")

# 3. Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
build_dir = os.path.join("android", "app", "build")

if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

if os.path.exists(build_dir):
    shutil.rmtree(build_dir)
    print("🧹 Cleared build directory.")

os.system("git add .")
os.system('git commit -m "fix: Master patch for namespace ares using nall and serializer"')
print("\n✅ Master patch complete!")
