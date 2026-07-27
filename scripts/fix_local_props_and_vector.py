import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Force ndk.dir in android/local.properties
local_props_path = os.path.join("android", "local.properties")
ndk_26_path = "/home/garyb/Android/Sdk/ndk/26.1.10909125"

props_lines = []
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props_lines = f.readlines()

new_props = []
has_ndk = False
for line in props_lines:
    if line.startswith("ndk.dir"):
        new_props.append(f"ndk.dir={ndk_26_path}\n")
        has_ndk = True
    else:
        new_props.append(line)

if not has_ndk:
    new_props.append(f"ndk.dir={ndk_26_path}\n")

with open(local_props_path, "w", encoding="utf-8") as f:
    f.writelines(new_props)
print(f"✏️ Updated android/local.properties with ndk.dir={ndk_26_path}")

# 2. Fix nall/vector.hpp: Ensure serializer is forward-declared inside namespace nall
vector_path = os.path.join("nall", "nall", "vector.hpp")
if os.path.exists(vector_path):
    with open(vector_path, "r", encoding="utf-8") as f:
        v_content = f.read()

    # Clean previous insertion
    v_content = v_content.replace("namespace nall { struct serializer; }", "")

    # Place struct serializer; directly inside namespace nall {
    if "namespace nall {" in v_content:
        v_content = v_content.replace("namespace nall {", "namespace nall {\n  struct serializer;")
        with open(vector_path, "w", encoding="utf-8") as f:
            f.write(v_content)
        print("✏️ Inserted 'struct serializer;' directly inside 'namespace nall {' in vector.hpp.")

# 3. Clear CMake cache
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
os.system('git commit -m "fix: Update ndk.dir in local.properties and serializer forward declaration in vector.hpp"')
print("\n✅ Script completed!")
