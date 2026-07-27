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

    if "CMAKE_EXPORT_COMPILE_COMMANDS" not in content:
        content = content.replace("set(CMAKE_CXX_STANDARD_REQUIRED ON)",
                                  "set(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)")
        with open(cmake_path, "w", encoding="utf-8") as f:
            f.write(content)
        print("✏️ Enabled CMAKE_EXPORT_COMPILE_COMMANDS in CMakeLists.txt.")

os.system("git add .")
os.system('git commit -m "chore: Enable CMAKE_EXPORT_COMPILE_COMMANDS for IDE clangd indexing"')
print("\n✅ IDE Indexer configuration updated!")
