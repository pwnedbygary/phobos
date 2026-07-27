import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Put forward-declaration of serializer directly inside nall/vector.hpp
vector_hpp_path = os.path.join("nall", "nall", "vector.hpp")
if os.path.exists(vector_hpp_path):
    with open(vector_hpp_path, "r", encoding="utf-8") as f:
        v_content = f.read()

    if "struct serializer;" not in v_content:
        v_content = v_content.replace("#pragma once", "#pragma once\nnamespace nall { struct serializer; }")
        with open(vector_hpp_path, "w", encoding="utf-8") as f:
            f.write(v_content)
        print("✏️ Forward-declared struct serializer; inside nall/vector.hpp.")

# 2. Force explicit ndkPath in android/app/build.gradle
gradle_path = os.path.join("android", "app", "build.gradle")
if os.path.exists(gradle_path):
    with open(gradle_path, "r", encoding="utf-8") as f:
        g_content = f.read()

    ndk_line = 'ndkPath "/home/garyb/Android/Sdk/ndk/26.1.10909125"'
    if "ndkPath" not in g_content:
        g_content = g_content.replace("android {", f"android {{\n    {ndk_line}")
        with open(gradle_path, "w", encoding="utf-8") as f:
            f.write(g_content)
        print("✏️ Forced explicit NDK 26 path in app/build.gradle.")

# 3. Wipe CMake cache and build output
cxx_dir = os.path.join("android", "app", ".cxx")
build_dir = os.path.join("android", "app", "build")

if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared CMake .cxx cache.")

if os.path.exists(build_dir):
    shutil.rmtree(build_dir)
    print("🧹 Cleared build directory.")

# 4. Commit to Git
os.system("git add .")
os.system('git commit -m "fix: Forward declare serializer in vector.hpp and force explicit NDK 26 path"')
print("\n✅ Script completed!")
