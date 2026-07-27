import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Update ares/ares/ares.hpp with nall/serializer.hpp and using namespace nall
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. Add #include <nall/serializer.hpp> right after #include <nall/nall.hpp>
    if "serializer.hpp" not in content:
        if "#include <nall/nall.hpp>" in content:
            content = content.replace("#include <nall/nall.hpp>", "#include <nall/nall.hpp>\n#include <nall/serializer.hpp>")
        else:
            content = "#include <nall/serializer.hpp>\n" + content

    # 2. Add using namespace nall; inside namespace ares {
    if "using namespace nall;" not in content:
        if "namespace ares {" in content:
            content = content.replace("namespace ares {", "namespace ares {\n  using namespace nall;")
        else:
            content += "\nnamespace ares {\n  using namespace nall;\n}\n"

    with open(ares_hpp_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("✏️ Updated ares/ares/ares.hpp with serializer and namespace nall.")

# Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Include nall/serializer.hpp and using namespace nall in ares/ares.hpp"')
print("\n✅ Final serializer patch completed!")
