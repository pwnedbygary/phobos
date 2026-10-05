#pragma once

#include <functional>
#include <string>
#include <vector>

//The loader: puts a PSP program in memory, as the PSP's kernel would before starting it.
//
//PSP programs are ELF files (the executable format of most Unix systems) for a MIPS CPU, of two kinds:
//  - a static executable (ELF type 2), linked to run at fixed addresses, as old homebrew is;
//  - a relocatable module, a PRX (ELF type 0xffa0), linked as if it started at address 0 and moved wherever the
//    kernel puts it: a list of relocations says which words hold addresses, so they can be changed to match.
//Games and newer homebrew are PRXs. Usually the program sits inside an EBOOT.PBP, a container that also holds
//its title (PARAM.SFO), icons and background music; the program is the part called DATA.PSP.
//
//Every PSP program has a "module info" (its name, version and global pointer, and where two tables are):
//  - the imports: for each system library the program uses (sceDisplay, ThreadManForUser...), the functions it
//    calls, each named by a NID (a 32-bit number standing for the function's name), and for each a stub, eight
//    bytes the program jumps to when it calls that function. The loader writes "jr ra; syscall n" there: the
//    syscall reaches the HLE kernel (Allegrex::syscallHook) with a code standing for that library and NID, which
//    answers it and returns straight to the caller.
//  - the exports: what the program offers others, at least module_start, where it begins.
//
//A retail game's programs come encrypted ("~PSP"): the loader has them decrypted first (decrypt.cpp).
//
//Not yet: the newer packed relocation format some retail modules use (PT_PSP_REL2), and imports of variables from
//other modules.

namespace ares::PlayStationPortable {

struct Memory;

//What the loader found in a program and where it put it.
struct Module {
  struct Segment { u32 address, size; };  //a part of the program in memory: where, and how big
  struct Import { std::string library; u32 nid, stub; };
  struct Export { std::string library; u32 nid, address; bool variable; };

  std::string name;        //from the module info
  u16 attributes = 0;      //PSP_MODULE_USER, KERNEL...
  u8 version[2] = {};      //major, minor
  bool relocatable = false;
  u32 base = 0;            //where a PRX was put (0 for a static executable)
  u32 entry = 0;           //where its first thread starts
  u32 gp = 0;              //the value of its global pointer register
  u32 moduleInfo = 0;      //where its module info is
  std::vector<Segment> segments;
  std::vector<Import> imports;
  std::vector<Export> exports;
  std::vector<std::string> skipped;  //what the loader left out (a variable import, say), worth reporting
};

struct Loader {
  //The code the HLE kernel will see in syscall for a library's function: the loader asks once per import.
  using ImportCode = std::function<auto (const std::string& library, u32 nid) -> u32>;

  //loader.cpp
  static auto programInPBP(const u8* data, u64 size, u64& offset, u64& length) -> bool;
  static auto load(Memory& memory, const u8* data, u64 size, u32 base, const ImportCode& importCode, Module& module)
    -> std::string;
};

}
