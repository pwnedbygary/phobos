#include "loader.hpp"
#include "crypto.hpp"
#include "../memory/memory.hpp"

namespace ares::PlayStationPortable {

namespace {

//ELF's numbers for what the loader looks at.
enum : u32 {
  ElfExecutable  = 2,           //e_type: a static executable
  ElfPRX         = 0xffa0,      //e_type: Sony's relocatable module
  ElfMIPS        = 8,           //e_machine
  SegmentLoad    = 1,           //p_type: bytes to put in memory
  SonyRelocations       = 0x7000'00a0,  //p_type and sh_type: a PRX's relocations
  SonyPackedRelocations = 0x7000'00a1,  //p_type: the newer packed form (not yet)
  HeaderSize        = 52,
  SegmentEntrySize  = 32,
  SectionEntrySize  = 40,
  ModuleInfoSize    = 52,
};

//MIPS relocation types: what kind of address a relocated word holds.
enum : u32 {
  RelocationNone = 0,
  Relocation16   = 1,  //a 16-bit address
  Relocation32   = 2,  //a whole 32-bit address (a pointer in data)
  Relocation26   = 4,  //the 26-bit target of j or jal (a word address within the current 256 MiB)
  RelocationHi16 = 5,  //the upper half of an address built by lui ...
  RelocationLo16 = 6,  //... and its lower half, in the addiu, ori, lw or sw that follows
};

//Reads the file, a little-endian word at a time, refusing to read past its end.
struct File {
  const u8* data;
  u64 size;
  auto has(u64 offset, u64 length) const -> bool { return offset <= size && length <= size - offset; }
  auto read16(u64 offset) const -> u32 { return data[offset] | data[offset + 1] << 8; }
  auto read32(u64 offset) const -> u32 { return read16(offset) | read16(offset + 2) << 16; }
};

auto hex(u32 value) -> std::string {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08x", value);
  return text;
}

}

//An EBOOT.PBP holds eight files one after another, each found by its offset in the header: PARAM.SFO, ICON0.PNG,
//ICON1.PMF, PIC0.PNG, PIC1.PNG, SND0.AT3, DATA.PSP (the program) and DATA.PSAR. Finds the program; false if this
//isn't a PBP (or a broken one).
auto Loader::programInPBP(const u8* data, u64 size, u64& offset, u64& length) -> bool {
  File file{data, size};
  if(!file.has(0, 40) || file.read32(0) != 0x5042'5000) return false;  //"\0PBP"
  u64 start = file.read32(8 + 6 * 4), end = file.read32(8 + 7 * 4);
  if(end == 0 || end < start) end = size;  //some PBPs leave DATA.PSAR's offset out
  if(start > end || end > size) return false;
  offset = start;
  length = end - start;
  return true;
}

//The addresses an ELF program's segments take, from the first to the end of the last, as it was linked (a PRX's
//count from 0), and whether it's a PRX: what the kernel needs to find room for a module before loading it. False if
//it isn't a MIPS ELF program, or has nothing to load.
auto Loader::extent(const u8* data, u64 size, u32& low, u32& high, bool& relocatable) -> bool {
  File file{data, size};
  if(!file.has(0, HeaderSize) || file.read32(0) != 0x464c'457f || data[4] != 1 || data[5] != 1) return false;
  u32 type = file.read16(16), table = file.read32(28), count = file.read16(44);
  if(file.read16(18) != ElfMIPS || (type != ElfExecutable && type != ElfPRX)) return false;
  if(file.read16(42) != SegmentEntrySize || !file.has(table, u64(count) * SegmentEntrySize)) return false;
  u64 lowest = ~0ull, highest = 0;
  for(u32 n = 0; n < count; n++) {
    u64 at = table + u64(n) * SegmentEntrySize;
    if(file.read32(at) != SegmentLoad) continue;
    u64 address = file.read32(at + 8);
    lowest = std::min(lowest, address);
    highest = std::max(highest, address + file.read32(at + 20));
  }
  if(highest <= lowest || highest > 0xffff'ffff) return false;
  low = lowest, high = highest, relocatable = type == ElfPRX;
  return true;
}

//Puts the program in data into memory: a PRX at base, a static executable where it was linked to go. Patches every
//import's stub to "jr ra; syscall importCode(library, nid)". Returns why it couldn't, or nothing when it did.
auto Loader::load(Memory& memory, const u8* data, u64 size, u32 base, const ImportCode& importCode, Module& module)
  -> std::string {
  module = {};
  //An encrypted program (a retail game's, "~PSP") is decrypted first (decrypt.cpp), and the ELF inside it loaded.
  std::vector<u8> decrypted;
  unwrapProgram(data, size);
  if(encryptedProgram(data, size)) {
    if(auto why = decryptProgram(data, size, decrypted); !why.empty()) return why;
    data = decrypted.data(), size = decrypted.size();
  }
  File file{data, size};
  if(!file.has(0, HeaderSize) || file.read32(0) != 0x464c'457f) return "not an ELF file";
  if(data[4] != 1 || data[5] != 1) return "not a 32-bit little-endian ELF file";
  u32 type = file.read16(16), machine = file.read16(18);
  if(machine != ElfMIPS) return "not a MIPS program";
  if(type != ElfExecutable && type != ElfPRX) return "an ELF file of type " + hex(type) + ", not a program";
  module.relocatable = type == ElfPRX;
  module.base = module.relocatable ? base : 0;
  u32 relocation = module.base;  //what's added to the addresses the program was linked with

  //Program headers: the segments to put in memory.
  u32 segmentTable = file.read32(28), segmentCount = file.read16(44);
  u32 sectionTable = file.read32(32), sectionCount = file.read16(48), sectionNames = file.read16(50);
  if(file.read16(42) != SegmentEntrySize || !file.has(segmentTable, u64(segmentCount) * SegmentEntrySize)) {
    return "its program headers are broken";
  }
  struct Header { u32 type, offset, address, physical, fileSize, memorySize; };
  std::vector<Header> segments;
  for(u32 n = 0; n < segmentCount; n++) {
    u64 at = segmentTable + u64(n) * SegmentEntrySize;
    segments.push_back({file.read32(at), file.read32(at + 4), file.read32(at + 8), file.read32(at + 12),
                        file.read32(at + 16), file.read32(at + 20)});
  }
  for(auto& segment : segments) {
    if(segment.type != SegmentLoad) continue;
    if(segment.fileSize > segment.memorySize || !file.has(segment.offset, segment.fileSize)) {
      return "a segment runs past the end of the file";
    }
    u32 address = relocation + segment.address;
    if(!memory.reaches(address, segment.memorySize ? segment.memorySize : 1)) {
      return "a segment (" + hex(address) + ", " + std::to_string(segment.memorySize) + " bytes) doesn't fit in memory";
    }
    memory.copyIn(address, data + segment.offset, segment.fileSize);
    memory.fill(address + segment.fileSize, 0, segment.memorySize - segment.fileSize);  //zeroed data (.bss)
    module.segments.push_back({address, segment.memorySize});
  }
  if(module.segments.empty()) return "no segments to load";

  //Section headers, when the program kept them: names find the module info and the relocations.
  struct Section { std::string name; u32 type, address, offset, size; };
  std::vector<Section> sections;
  if(sectionCount && file.read16(46) == SectionEntrySize && file.has(sectionTable, u64(sectionCount) * SectionEntrySize)) {
    u64 namesAt = sectionNames < sectionCount ? sectionTable + u64(sectionNames) * SectionEntrySize : 0;
    u32 namesOffset = namesAt ? file.read32(namesAt + 16) : 0, namesSize = namesAt ? file.read32(namesAt + 20) : 0;
    for(u32 n = 0; n < sectionCount; n++) {
      u64 at = sectionTable + u64(n) * SectionEntrySize;
      std::string name;
      for(u64 c = namesOffset + u64(file.read32(at)); namesAt && c < u64(namesOffset) + namesSize && file.has(c, 1) && data[c]; c++) {
        name.push_back(char(data[c]));
      }
      sections.push_back({name, file.read32(at + 4), file.read32(at + 12), file.read32(at + 16), file.read32(at + 20)});
    }
  }

  //Relocations (PRX only), from its relocation sections, or from its program headers if it kept no sections. Each is
  //eight bytes: an offset, and an info word holding the type (bits 0-7), the segment the offset counts from (bits
  //8-15) and the segment whose address is added to the word (bits 16-23): a relocated word holds an address
  //within that segment. (Sony's tools and pspdev's alike number segments by their program headers. pspdev's PRXs
  //have a single segment, starting at 0; retail modules with more will show the segment numbers matter.)
  if(module.relocatable) {
    for(auto& segment : segments) {
      if(segment.type == SonyPackedRelocations) return "packed relocations (PT_PSP_REL2) aren't supported yet";
    }
    std::vector<std::pair<u32, u32>> tables;  //file offset and size of each list of relocations
    for(auto& section : sections) if(section.type == SonyRelocations) tables.push_back({section.offset, section.size});
    if(tables.empty()) {
      for(auto& segment : segments) {
        if(segment.type == SonyRelocations) tables.push_back({segment.offset, segment.fileSize});
      }
    }
    auto segmentAddress = [&](u32 index) { return relocation + segments[index].address; };
    struct Pending { u32 address, add; };
    std::vector<Pending> pendingHi16;  //lui words waiting for the lower half that completes their address
    for(auto [offset, length] : tables) {
      if(!file.has(offset, length)) return "a relocation table runs past the end of the file";
      for(u32 n = 0; n + 8 <= length; n += 8) {
        u32 info = file.read32(offset + n + 4);
        u32 kind = info & 0xff, from = info >> 8 & 0xff, to = info >> 16 & 0xff;
        if(from >= segments.size() || to >= segments.size()) return "a relocation names a segment that isn't there";
        u32 address = segmentAddress(from) + file.read32(offset + n);
        u32 add = segmentAddress(to);
        if(kind == RelocationNone) continue;
        if(!memory.reaches(address, 4)) return "a relocation points outside the program: " + hex(address);
        u32 word = memory.read(4, address);
        switch(kind) {
        case Relocation32:
          memory.write(4, address, word + add);
          break;
        case Relocation26:  //the target is a word address: the jump keeps the top four bits of where it is
          memory.write(4, address, (word & 0xfc00'0000) | ((word + (add >> 2)) & 0x03ff'ffff));
          break;
        case RelocationHi16:
          pendingHi16.push_back({address, add});
          break;
        case Relocation16:
        case RelocationLo16: {
          //An address built by lui (the upper half) and an instruction that adds a signed lower half: moving it may
          //carry into the upper half, which is why each lui waits for its lower half. One lui can serve several.
          //A plain 16-bit relocation gives the same 16 bits, and completes a waiting lui the same way: some retail
          //modules pair them like that.
          s32 low = s16(word & 0xffff);
          for(auto& pending : pendingHi16) {
            u32 hi = memory.read(4, pending.address);
            u32 full = (hi << 16) + u32(low) + pending.add;
            memory.write(4, pending.address, (hi & 0xffff'0000) | ((full + 0x8000) >> 16 & 0xffff));
          }
          pendingHi16.clear();
          memory.write(4, address, (word & 0xffff'0000) | ((u32(low) + add) & 0xffff));
          break;
        }
        default:
          return "a relocation of type " + std::to_string(kind) + ", which the loader doesn't know";
        }
      }
    }
    for(auto& pending : pendingHi16) {  //an upper half with no lower half after it: moved on its own
      u32 hi = memory.read(4, pending.address);
      memory.write(4, pending.address, (hi & 0xffff'0000) | (((hi << 16) + pending.add + 0x8000) >> 16 & 0xffff));
    }
  }

  //The module info: its section if the program kept sections, or else (in a PRX) where the first program header's
  //physical address field says, as a file offset.
  for(auto& section : sections) {
    if(section.name == ".rodata.sceModuleInfo") module.moduleInfo = relocation + section.address;
  }
  if(!module.moduleInfo && module.relocatable && !segments.empty()) {
    auto& first = segments[0];
    u32 offset = first.physical & 0x7fff'ffff;
    if(offset >= first.offset) module.moduleInfo = relocation + first.address + (offset - first.offset);
  }
  if(!module.moduleInfo || !memory.reaches(module.moduleInfo, ModuleInfoSize)) return "no module info";
  u32 info = module.moduleInfo;
  module.attributes = memory.read(2, info);
  module.version[0] = memory.read(1, info + 2);
  module.version[1] = memory.read(1, info + 3);
  module.name = memory.readString(info + 4, 28);
  module.gp = memory.read(4, info + 32);
  u32 exportsStart = memory.read(4, info + 36), exportsEnd = memory.read(4, info + 40);
  u32 importsStart = memory.read(4, info + 44), importsEnd = memory.read(4, info + 48);
  module.entry = relocation + file.read32(24);

  //The imports: one entry per library, each len words long: the library's name, its version and attributes, the
  //entry's length, counts of variables and functions, and where the NIDs and the stubs are.
  for(u32 at = importsStart; at < importsEnd;) {
    if(!memory.reaches(at, 20)) return "the import table is broken";
    u32 length = memory.read(1, at + 8);
    u32 variables = memory.read(1, at + 9), functions = memory.read(2, at + 10);
    u32 nids = memory.read(4, at + 12), stubs = memory.read(4, at + 16);
    std::string library = memory.readString(memory.read(4, at), 64);
    if(length < 5) return "an import entry is too short";
    if(functions && (!memory.reaches(nids, functions * 4) || !memory.reaches(stubs, functions * 8))) {
      return "the imports from " + library + " point outside memory";
    }
    for(u32 n = 0; n < functions; n++) {
      u32 nid = memory.read(4, nids + n * 4), stub = stubs + n * 8;
      u32 code = importCode ? importCode(library, nid) : 0;
      memory.write(4, stub, 0x03e0'0008);                    //jr ra
      memory.write(4, stub + 4, (code & 0xf'ffff) << 6 | 0x0c);  //syscall code, in the delay slot
      module.imports.push_back({library, nid, stub});
    }
    if(variables) module.skipped.push_back(std::to_string(variables) + " variable imports from " + library);
    at += length * 4;
  }

  //The exports: one entry per library the module offers, each len words: its name (none for the module's own
  //entry points, such as module_start), version and attributes, length, counts, and a table of all the NIDs,
  //functions first, followed by the address of each.
  for(u32 at = exportsStart; at < exportsEnd;) {
    if(!memory.reaches(at, 16)) return "the export table is broken";
    u32 length = memory.read(1, at + 8);
    u32 variables = memory.read(1, at + 9), functions = memory.read(2, at + 10), table = memory.read(4, at + 12);
    u32 namePointer = memory.read(4, at);
    std::string library = namePointer ? memory.readString(namePointer, 64) : "";
    if(length < 4) return "an export entry is too short";
    u32 count = functions + variables;
    if(count && !memory.reaches(table, count * 8)) return "the exports of " + module.name + " point outside memory";
    for(u32 n = 0; n < count; n++) {
      module.exports.push_back({library, memory.read(4, table + n * 4), memory.read(4, table + (count + n) * 4),
                                n >= functions});
    }
    at += length * 4;
  }
  return {};
}

}
