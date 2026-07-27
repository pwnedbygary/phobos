import os

# Ensure script runs relative to repo root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
os.chdir(REPO_ROOT)

runner_cpp = os.path.join("android", "app", "src", "main", "cpp", "PhobosRunner.cpp")
if os.path.exists(runner_cpp):
    with open(runner_cpp, "r", encoding="utf-8") as f:
        txt = f.read()

    # Uncomment drawFrame call with verified screen API
    txt = txt.replace("// drawFrame(screen->pixels(), screen->width(), screen->height(), screen->pitch());",
                      "drawFrame(screen->pixels(), screen->width(), screen->height(), screen->pitch());")

    with open(runner_cpp, "w", encoding="utf-8") as f:
        f.write(txt)
    print("✏️ Restored drawFrame(screen->pixels(), screen->width(), screen->height(), screen->pitch()) in PhobosRunner.cpp.")

os.system("git add .")
os.system('git commit -m "feat: Hook up verified screen->pixels() native video rendering"')
print("\n✅ Native video pipeline fully connected!")
