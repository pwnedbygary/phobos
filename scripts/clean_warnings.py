import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Remove ndkPath line from app/build.gradle to clear [CXX1100] warning
gradle_path = os.path.join("android", "app", "build.gradle")
if os.path.exists(gradle_path):
    with open(gradle_path, "r", encoding="utf-8") as f:
        g_content = f.read()

    # Remove ndkPath line so local.properties handles it cleanly
    lines = g_content.splitlines()
    new_lines = [l for l in lines if "ndkPath" not in l]
    with open(gradle_path, "w", encoding="utf-8") as f:
        f.write("\n".join(new_lines) + "\n")
    print("✏️ Removed duplicate ndkPath from app/build.gradle.")

# 2. Add #include <cstdint> to ares/resource/resource.hpp to fix uint8_t squiggly
resource_hpp_path = os.path.join("ares", "resource", "resource.hpp")
if os.path.exists(resource_hpp_path):
    with open(resource_hpp_path, "w", encoding="utf-8") as f:
        f.write("""#pragma once
#include <cstdint>
#include <nall/vector.hpp>
#include <nall/string.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};
}
""")
    print("✏️ Added <cstdint> to ares/resource/resource.hpp.")

# 3. Commit
os.system("git add .")
os.system('git commit -m "fix: Resolve AGP warning CXX1100 and add cstdint to resource.hpp"')
print("\n✅ Warning resolution completed!")
