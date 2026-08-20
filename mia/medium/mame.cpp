// Helper/Utility for importing roms for systems that are using MAME romsets
struct Mame : Medium {
  auto loadRoms(string location, Markup::Node& info, string sectionName) -> std::vector<u8>;
  auto loadRomFile(string location, string filename, Markup::Node& currentInfo) -> std::vector<u8>;
  auto endianSwap(std::vector<u8>& memory, u32 address = 0, int size = -1) -> void;
};

auto Mame::loadRoms(string location, Markup::Node& info, string sectionName) -> std::vector<u8> {
  std::vector<u8> output;

  Decode::ZIP archive;
  if(!location.iendsWith(".zip")) return output;
  if(!archive.open(location)) return output;

  string filename = {};
  std::vector<u8> input = {};
  u32 readOffset = 0;
  string loadType = {};

  for(auto section : info[{"game/", sectionName}]) {
    if(section.name() == "rom") {
      if(section["type"] && section["type"].string() != "continue") loadType = section["type"].string();
      if(section["name"]) {
        filename = section["name"].string().strip();
        if(filename) {
          input = {};
          input = loadRomFile(location, filename, info);
          readOffset = 0;
          if (input.empty()) return output;
        }
      }

      auto writeOffset = section["offset"].natural();
      auto size = section["size"].natural();

      if(loadType == "load32_word_swap") {
        if(output.size() < writeOffset + size * 2) output.resize(writeOffset + size * 2);
        for(u32 index : range(size >> 1)) {
          auto source = readOffset + index * 2;
          auto target = writeOffset + index * 4;
          output[target + 0] = input[source + 1];
          output[target + 1] = input[source + 0];
        }
        readOffset += size;
        continue;
      }

      auto startIndex = 0;
      auto increment = 1;
      if(loadType == "load16_byte") {
        increment = 2;
        size *= 2;
        if(writeOffset & 1) {
          startIndex = 1;
          writeOffset--;
        }
      }

      if(output.size() < writeOffset + size) output.resize(writeOffset + size);

      for(auto index = startIndex; index < size; index += increment) {
        if(loadType == "fill") {
          output[index + writeOffset] = section["value"].natural();
          continue;
        }

        output[index + writeOffset] = input[readOffset++];
      }

      if(loadType == "load16_word_swap") endianSwap(output, section["offset"].natural(), size);
    }
  }
  return output;
}

auto Mame::loadRomFile(string location, string filename, Markup::Node& info) -> std::vector<u8> {
  Decode::ZIP archive;
  if(!archive.open(location)) return {};

  for(auto& file : archive.file) {
    // some romsets (e.g. redumps) store files under a sub-directory; match by basename
    string base = file.name;
    s32 slash = -1;
    for(s32 i = (s32)base.size() - 1; i >= 0; i--) if(base[i] == '/') { slash = i; break; }
    if(slash >= 0) base = slice(base, slash + 1, (s32)base.size() - slash - 1);
    if(base.iequals(filename)) {
      return archive.extract(file);
    }
  }

  if(auto parent = info["game/parent"].string()) {
    auto manifest = manifestDatabaseArcade(parent);
    auto document = BML::unserialize(manifest);
    if(!document) return {};
    location = {Location::path(location), "/", document["game/name"].string(), ".zip"};
    return loadRomFile(location, filename, document);
  }

  return {};
};


auto Mame::endianSwap(std::vector<u8>& memory, u32 address, int size) -> void {
  if(size < 0) size = (int)memory.size();
  for(u32 index = 0; index < (u32)size; index += 2) {
    swap(memory[address + index + 0], memory[address + index + 1]);
  }
}
