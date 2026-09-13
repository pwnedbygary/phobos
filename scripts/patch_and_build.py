import os
import re
import sys
from pathlib import Path

def restore_unity_builds(project_root):
    # Phase 1: Strips erroneous includes from component .cpp files.
    ares_dir = project_root / 'ares'
    if not ares_dir.exists():
        print(f"[!] Error: 'ares' directory not found at {ares_dir}")
        return False

    inline_hpp_re = re.compile(r'^\s*#include\s*[<"].*inline\.hpp[>"]\s*\n?', re.MULTILINE)
    
    cores = [p.name for p in ares_dir.iterdir() if p.is_dir() and p.name != 'ares' 
             and (p / f"{p.name}.cpp").exists() and (p / f"{p.name}.hpp").exists()]

    patched_files = 0
    for core in cores:
        core_dir = ares_dir / core
        core_hpp = core_dir / f"{core}.hpp"
        core_cpp = core_dir / f"{core}.cpp"
        
        if core_hpp.exists():
            original_text = core_hpp.read_text(encoding='utf-8')
            new_text = inline_hpp_re.sub('', original_text)
            if new_text != original_text:
                core_hpp.write_text(new_text, encoding='utf-8')
                patched_files += 1

        core_hpp_re = re.compile(rf'^\s*#include\s*[<"].*{core}\.hpp[>"]\s*\n?', re.MULTILINE)
        ares_hpp_re = re.compile(r'^\s*#include\s*[<"].*ares\.hpp[>"]\s*\n?', re.MULTILINE)

        for cpp_file in core_dir.rglob("*.cpp"):
            if cpp_file == core_cpp:
                continue 
            original_text = cpp_file.read_text(encoding='utf-8')
            new_text = core_hpp_re.sub('', original_text)
            new_text = ares_hpp_re.sub('', new_text)
            new_text = inline_hpp_re.sub('', new_text)
            if new_text != original_text:
                cpp_file.write_text(new_text, encoding='utf-8')
                patched_files += 1

    if patched_files > 0:
        print(f"[*] Phase 1: Cleaned up {patched_files} component file(s).")
    else:
        print("[*] Phase 1: Component files are already clean.")
    return True

def restore_master_cpp_files(project_root):
    # Phase 2: Ensures master core files (like a26.cpp) correctly include their global headers.
    ares_dir = project_root / 'ares'
    cores = [p.name for p in ares_dir.iterdir() if p.is_dir() and p.name != 'ares' 
             and (p / f"{p.name}.cpp").exists() and (p / f"{p.name}.hpp").exists()]

    patched_files = 0
    for core in cores:
        core_cpp = ares_dir / core / f"{core}.cpp"
        if core_cpp.exists():
            original_text = core_cpp.read_text(encoding='utf-8')
            has_ares_hpp = re.search(r'^\s*#include\s*[<"]ares/ares\.hpp[>"]', original_text, re.MULTILINE)
            has_core_hpp = re.search(rf'^\s*#include\s*[<"]{core}\.hpp[>"]', original_text, re.MULTILINE)
            
            prepend = ""
            if not has_ares_hpp:
                prepend += "#include <ares/ares.hpp>\n"
            if not has_core_hpp:
                prepend += f'#include "{core}.hpp"\n'
                
            if prepend:
                new_text = prepend + "\n" + original_text
                core_cpp.write_text(new_text, encoding='utf-8')
                patched_files += 1

    if patched_files > 0:
        print(f"[*] Phase 2: Restored missing master headers in {patched_files} file(s).")
    else:
        print("[*] Phase 2: Master .cpp files are already clean.")
    return True

def fix_global_ares_hpp(project_root):
    # Phase 3: Repairs the global ares.hpp file
    ares_hpp_path = project_root / 'ares' / 'ares' / 'ares.hpp'
    if not ares_hpp_path.exists():
        print("[!] Warning: ares/ares/ares.hpp not found!")
        return False
        
    text = ares_hpp_path.read_text(encoding='utf-8')
    original_text = text
    
    # 1. Clean up previous botched injections
    text = re.sub(r'^\s*// Re-injected by Phobos patcher\s*\n', '', text, flags=re.MULTILINE)
    
    # 2. Strip these headers from anywhere in the file so we don't have duplicates
    core_headers = [
        "ares/memory/memory.hpp",
        "ares/node/node.hpp",
        "ares/scheduler/thread.hpp",
        "ares/scheduler/scheduler.hpp"
    ]
    for header in core_headers + ["ares/inline.hpp"]:
        safe_header = header.replace(".", r"\.")
        text = re.sub(rf'^\s*#include\s*[<"]{safe_header}[>"]\s*\n?', '', text, flags=re.MULTILINE)

    # 3. Inject core headers BEFORE platform.hpp (inside the namespace block)
    platform_re = re.compile(r'^(\s*#include\s*[<"].*platform\.hpp[>"])', re.MULTILINE)
    
    injection = ""
    for header in core_headers:
        injection += f"  #include <{header}>\n"
        
    if platform_re.search(text):
        text = platform_re.sub(injection + r'\1', text)
    else:
        # Fallback if platform.hpp isn't found
        last_brace = text.rfind('}')
        if last_brace != -1:
            text = text[:last_brace] + injection + text[last_brace:]
            
    # 4. Ensure inline.hpp is at the very end of the file (OUTSIDE the namespace)
    text = text.strip() + "\n\n#include <ares/inline.hpp>\n"
    
    if text != original_text:
        ares_hpp_path.write_text(text, encoding='utf-8')
        print(f"[*] Phase 3: Repaired missing framework includes in ares.hpp (placed before platform.hpp)")
    else:
        print(f"[*] Phase 3: Global ares.hpp is already intact.")
        
    return True

if __name__ == '__main__':
    script_dir = Path(__file__).parent.resolve()
    project_root = script_dir.parent if (script_dir.parent / "ares").exists() else script_dir
    
    print("[*] Starting Phobos/Ares codebase patching...")
    restore_unity_builds(project_root)
    restore_master_cpp_files(project_root)
    fix_global_ares_hpp(project_root)
    
    print("\n[+] Patching complete! You can now run the build manually in Android Studio.")
