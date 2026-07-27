import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Add #include <nall/serializer.hpp> directly into ares/ares/ares.hpp
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    if "serializer.hpp" not in content:
        if "#include <nall/nall.hpp>" in content:
            content = content.replace("#include <nall/nall.hpp>", "#include <nall/nall.hpp>\n#include <nall/serializer.hpp>")
        else:
            content = "#include <nall/serializer.hpp>\n" + content

        with open(ares_hpp_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✏️ Added #include <nall/serializer.hpp> to ares/ares/ares.hpp.")

os.system("git add .")
os.system('git commit -m "fix: Include nall/serializer.hpp in ares.hpp"')
print("\n✅ Patched ares.hpp!")
