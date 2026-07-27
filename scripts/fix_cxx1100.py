import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# Remove duplicate ndk.dir from local.properties so app/build.gradle ndkPath rules exclusively
local_props_path = os.path.join("android", "local.properties")
if os.path.exists(local_props_path):
    with open(local_props_path, "r", encoding="utf-8") as f:
        props = f.readlines()
    clean_props = [p for p in props if not p.startswith("ndk.dir")]
    with open(local_props_path, "w", encoding="utf-8") as f:
        f.writelines(clean_props)
    print("✏️ Removed duplicate ndk.dir from local.properties.")

os.system("git add .")
os.system('git commit -m "fix: Remove duplicate ndk.dir from local.properties to resolve CXX1100"')
print("\n✅ CXX1100 resolved!")
