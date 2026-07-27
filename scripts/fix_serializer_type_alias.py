import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Update ares/ares/ares.hpp with explicit serializer type alias
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Ensure #include <nall/serializer.hpp> is present
    if "serializer.hpp" not in content:
        if "#include <nall/nall.hpp>" in content:
            content = content.replace("#include <nall/nall.hpp>", "#include <nall/nall.hpp>\n#include <nall/serializer.hpp>")
        else:
            content = "#include <nall/serializer.hpp>\n" + content

    # Add explicit global type alias using serializer = nall::serializer;
    if "using serializer = nall::serializer;" not in content:
        content += "\nusing nall::serializer;\nusing serializer = nall::serializer;\n"

    # Ensure using namespace nall and type alias inside namespace ares {
    if "using namespace nall;" not in content:
        if "namespace ares {" in content:
            content = content.replace("namespace ares {", "namespace ares {\n  using namespace nall;\n  using serializer = nall::serializer;")

    with open(ares_hpp_path, "w", encoding="utf-8") as f:
        f.write(content)

    print("✏️ Added explicit 'using serializer = nall::serializer;' type alias to ares/ares/ares.hpp.")

# Clear CMake cache
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Add explicit using serializer = nall::serializer type alias in ares.hpp"')
print("\n✅ Serializer type alias patch complete!")
