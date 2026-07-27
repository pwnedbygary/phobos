import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Include nall/serializer.hpp in ares/ares.hpp so all cores see it
ares_hpp_path = os.path.join("ares", "ares", "ares.hpp")
if os.path.exists(ares_hpp_path):
    with open(ares_hpp_path, "r", encoding="utf-8") as f:
        content = f.read()

    if "serializer.hpp" not in content:
        new_content = "#include <nall/serializer.hpp>\n" + content
        if "using nall::serializer;" not in new_content:
            new_content += "\nusing nall::serializer;\n"

        with open(ares_hpp_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print("✏️ Patched ares/ares.hpp to include nall/serializer.hpp.")

# 2. Patch PhobosRunner.cpp to use correct screen->pixels() function call
runner_cpp = os.path.join("android", "app", "src", "main", "cpp", "PhobosRunner.cpp")
if os.path.exists(runner_cpp):
    with open(runner_cpp, "r", encoding="utf-8") as f:
        cpp_txt = f.read()

    # Fix function call for pixel buffer
    cpp_txt = cpp_txt.replace("screen->pixels->data()", "screen->pixels()")

    with open(runner_cpp, "w", encoding="utf-8") as f:
        f.write(cpp_txt)
    print("✏️ Patched PhobosRunner.cpp to use correct screen->pixels() API.")

# 3. Git Commit
os.system("git add .")
os.system('git commit -m "fix: Include serializer.hpp in ares.hpp and fix PhobosRunner pixels API"')
print("\n✅ Node API and Serializer patched successfully!")