//Builds PSP programs in memory for the loader's tests, so no binary goes in the repository: an ELF header, a program
//header per segment, and (unless left out, as a stripped program would) section headers with their names.
#pragma once

#include "system.hpp"

#include <string>

namespace allegrex_test::psp {

struct Bytes {
  std::vector<u8> data;
  auto at(u32 offset) -> void { if(data.size() < offset) data.resize(offset); }
  auto put8(u32 offset, u32 value) -> void { if(data.size() < offset + 1) data.resize(offset + 1); data[offset] = value; }
  auto put16(u32 offset, u32 value) -> void { put8(offset, value); put8(offset + 1, value >> 8); }
  auto put32(u32 offset, u32 value) -> void { put16(offset, value); put16(offset + 2, value >> 16); }
  auto putString(u32 offset, const std::string& text) -> void {
    for(u32 i = 0; i < text.size(); i++) put8(offset + i, u8(text[i]));
    put8(offset + text.size(), 0);
  }
};

struct ElfBuilder {
  struct Segment { u32 type = 1, address = 0, physical = 0; Bytes bytes; u32 memorySize = 0; };
  struct Section { std::string name; u32 type = 1, address = 0; Bytes bytes; };  //bytes: a relocation section's entries

  u32 type = 0xffa0;  //a PRX; 2 for a static executable
  u32 machine = 8;
  u32 entry = 0;
  bool withSections = true;
  std::vector<Segment> segments;
  std::vector<Section> sections;

  //Lays the file out: header, program headers, each segment's bytes (16-byte aligned), the sections' own bytes,
  //the section names, and the section headers. A section with no bytes of its own lives in the segment holding its
  //address, so its offset points there.
  auto build() -> std::vector<u8> {
    Bytes file;
    u32 offset = 52 + 32 * segments.size();
    std::vector<u32> segmentOffsets;
    for(auto& segment : segments) {
      offset = (offset + 15) & ~15u;
      segmentOffsets.push_back(offset);
      for(u32 i = 0; i < segment.bytes.data.size(); i++) file.put8(offset + i, segment.bytes.data[i]);
      offset += segment.bytes.data.size();
    }
    std::vector<u32> sectionOffsets;
    for(auto& section : sections) {
      if(section.bytes.data.empty()) {
        u32 found = 0;
        for(u32 n = 0; n < segments.size(); n++) {
          auto& segment = segments[n];
          if(section.address >= segment.address && section.address < segment.address + segment.bytes.data.size()) {
            found = segmentOffsets[n] + section.address - segment.address;
          }
        }
        sectionOffsets.push_back(found);
        continue;
      }
      offset = (offset + 3) & ~3u;
      sectionOffsets.push_back(offset);
      for(u32 i = 0; i < section.bytes.data.size(); i++) file.put8(offset + i, section.bytes.data[i]);
      offset += section.bytes.data.size();
    }
    u32 namesOffset = offset;
    std::vector<u32> nameOffsets;
    std::string names(1, '\0');
    for(auto& section : sections) { nameOffsets.push_back(names.size()); names += section.name + '\0'; }
    u32 namesName = names.size();
    names += ".shstrtab";
    names += '\0';
    for(u32 i = 0; i < names.size(); i++) file.put8(namesOffset + i, u8(names[i]));
    offset = (namesOffset + names.size() + 3) & ~3u;
    u32 sectionTable = offset;
    u32 sectionCount = withSections ? sections.size() + 2 : 0;  //the null section first, the names last
    if(withSections) {
      for(u32 n = 0; n < sections.size(); n++) {
        u32 at = sectionTable + (n + 1) * 40;
        file.put32(at, nameOffsets[n]);
        file.put32(at + 4, sections[n].type);
        file.put32(at + 12, sections[n].type == 1 ? sections[n].address : 0);
        file.put32(at + 16, sectionOffsets[n]);
        file.put32(at + 20, sections[n].bytes.data.empty() ? 4 : sections[n].bytes.data.size());
      }
      u32 at = sectionTable + (sections.size() + 1) * 40;
      file.put32(at, namesName);
      file.put32(at + 4, 3);  //SHT_STRTAB
      file.put32(at + 16, namesOffset);
      file.put32(at + 20, names.size());
      file.put32(at + 36, 0);
      file.at(at + 40);
    }

    file.put32(0, 0x464c'457f);
    file.put8(4, 1); file.put8(5, 1); file.put8(6, 1);
    file.put16(16, type);
    file.put16(18, machine);
    file.put32(20, 1);
    file.put32(24, entry);
    file.put32(28, 52);
    file.put32(32, withSections ? sectionTable : 0);
    file.put16(40, 52);
    file.put16(42, 32);
    file.put16(44, segments.size());
    file.put16(46, 40);
    file.put16(48, sectionCount);
    file.put16(50, withSections ? sections.size() + 1 : 0);
    for(u32 n = 0; n < segments.size(); n++) {
      u32 at = 52 + 32 * n;
      auto& segment = segments[n];
      file.put32(at, segment.type);
      file.put32(at + 4, segmentOffsets[n]);
      file.put32(at + 8, segment.address);
      file.put32(at + 12, segment.physical);
      file.put32(at + 16, segment.bytes.data.size());
      file.put32(at + 20, segment.memorySize ? segment.memorySize : segment.bytes.data.size());
    }
    return file.data;
  }
};

//A relocation entry: the offset to patch, its type, and the segments for the offset and the added address.
inline auto relocation(Bytes& table, u32 offset, u32 type, u32 from = 0, u32 to = 0) -> void {
  u32 at = table.data.size();
  table.put32(at, offset);
  table.put32(at + 4, type | from << 8 | to << 16);
}

}
