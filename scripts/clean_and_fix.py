import os
import shutil

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Clear stale CMake CXX cache (which was locked to NDK 25)
cxx_dir = os.path.join("android", "app", ".cxx")
build_dir = os.path.join("android", "app", "build")

if os.path.exists(cxx_dir):
    shutil.rmtree(cxx_dir)
    print("🧹 Cleared stale CMake cache (.cxx directory).")

if os.path.exists(build_dir):
    shutil.rmtree(build_dir)
    print("🧹 Cleared stale build directory.")

# 2. Create jansson_config.h if missing
jansson_config_path = os.path.join("thirdparty", "jansson", "src", "jansson_config.h")
if not os.path.exists(jansson_config_path):
    os.makedirs(os.path.dirname(jansson_config_path), exist_ok=True)
    with open(jansson_config_path, "w", encoding="utf-8") as f:
        f.write("""#pragma once

#define JSON_INLINE inline
#define JSON_INTEGER_IS_LONG_LONG 1
#define JSON_HAVE_LOCALECONV 1
#define JSON_HAVE_ATOMIC_BUILTINS 1
#define JSON_HAVE_SYNC_BUILTINS 1
""")
    print("✏️ Created jansson_config.h in thirdparty/jansson/src/.")
else:
    print("ℹ️ jansson_config.h already exists.")

# 3. Git Commit
os.system("git add .")
os.system('git commit -m "fix: Clear stale CMake NDK 25 cache and add jansson_config.h"')
print("\n✅ Cleaned and patched! Now force-sync Gradle in Android Studio.")
