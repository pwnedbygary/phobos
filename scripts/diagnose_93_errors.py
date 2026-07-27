import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

print(f"📁 Working in repository root: {REPO_ROOT}")

# 1. Print current CMakeLists.txt configuration
print("\n=== CMakeLists.txt ===")
if os.path.exists("CMakeLists.txt"):
    with open("CMakeLists.txt", "r", encoding="utf-8") as f:
        print(f.read())

# 2. Inspect armdsp.cpp around line 42
armdsp_cpp = os.path.join("ares", "sfc", "coprocessor", "armdsp", "armdsp.cpp")
if os.path.exists(armdsp_cpp):
    print("\n=== armdsp.cpp lines 30-55 ===")
    with open(armdsp_cpp, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
        for i, line in enumerate(lines[29:55], start=30):
            print(f"{i}: {line}", end="")

# 3. Inspect ares/resource/resource.hpp
res_hpp = os.path.join("ares", "resource", "resource.hpp")
if os.path.exists(res_hpp):
    print("\n=== ares/resource/resource.hpp ===")
    with open(res_hpp, "r", encoding="utf-8") as f:
        print(f.read())
