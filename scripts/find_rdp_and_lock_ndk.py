import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Lock local.properties permanently to NDK 26
local_props_path = os.path.join("android", "local.properties")
ndk_26_path = "/home/garyb/Android/Sdk/ndk/26.1.10909125"

props = []
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props = f.readlines()

new_props = [p for p in props if not p.startswith("ndk.dir")]
new_props.append(f"ndk.dir={ndk_26_path}\n")

with open(local_props_path, "w", encoding="utf-8") as f:
    f.writelines(new_props)
print(f"✏️ Locked ndk.dir={ndk_26_path} in android/local.properties.")

# 2. Search for all RDP / Parallel Vulkan header files across the repository
print("\n=== SEARCHING FOR RDP / PARALLEL / VULKAN HEADERS ===")
matches = []
for root, dirs, files in os.walk("."):
    for f in files:
        if "rdp" in f.lower() or "parallel" in f.lower():
            rel_p = os.path.relpath(os.path.join(root, f), REPO_ROOT).replace("\\", "/")
            matches.append(rel_p)

for m in matches[:30]:
    print(f"  {m}")

# 3. Clear CMake cache so NDK 26 takes over
cxx_dir = os.path.join("android", "app", ".cxx")
if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("\n🧹 Cleared CMake .cxx cache.")

os.system("git add .")
os.system('git commit -m "fix: Lock NDK 26 in local.properties and locate parallel-rdp headers"')
print("\n✅ Lock script complete!")
