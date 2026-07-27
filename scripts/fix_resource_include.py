import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Update ares/resource/resource.hpp to use master <nall/nall.hpp>
resource_hpp_path = os.path.join("ares", "resource", "resource.hpp")
if os.path.exists(resource_hpp_path):
    with open(resource_hpp_path, "w", encoding="utf-8") as f:
        f.write("""#pragma once
#include <cstdint>
#include <nall/nall.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};
}
""")
    print("✏️ Updated ares/resource/resource.hpp to use <nall/nall.hpp>.")

# Commit
os.system("git add .")
os.system('git commit -m "fix: Use nall/nall.hpp in stub resource.hpp"')
print("\n✅ Resource header patched!")
