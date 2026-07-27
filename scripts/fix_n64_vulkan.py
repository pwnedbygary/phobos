import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

cmake_path = "CMakeLists.txt"
if os.path.exists(cmake_path):
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. Add VULKAN to compile definitions
    if "BUILD_RELEASE VULKAN" not in content:
        content = content.replace("add_compile_definitions(BUILD_RELEASE)", "add_compile_definitions(BUILD_RELEASE VULKAN)")

    # 2. Add vulkan to target_link_libraries
    if "aaudio log vulkan" not in content:
        content = content.replace("target_link_libraries(phobos_android android aaudio log)",
                                  "target_link_libraries(phobos_android android aaudio log vulkan)")

    with open(cmake_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("✏️ Added VULKAN definition and linked libvulkan.so in CMakeLists.txt.")

# Commit to git
os.system("git add .")
os.system('git commit -m "feat: Enable native Android Vulkan support for N64 core"')
print("\n✅ Vulkan pipeline enabled successfully!")
