//What a PRIM handed to the GE thread may change in memory (model.hpp): the emulation thread's side, and the GE
//thread's copy of it that checks every byte a finish or put-back changes (GeThread::checking).

//Where the frame buffer's target starts (GPU::targetFor(): the frame buffer of its width and format that starts where
//its page does, where a page holds whole rows of it, else where its row does), its row and column there, and how many
//rows it has (as many as fit in VRAM, up to 512). One whose PRIM reaches past its target's rows' end or last row, or
//is drawn with its depth buffer, starts where the frame buffer does.
auto GE::targetPlace(const PixelState& p, const Region& region, u32& address, u32& row, u32& column, u32& height)
  -> void {
  u32 bytes = p.format == 3 ? 4 : 2, rowBytes = p.stride * bytes;
  address = p.frameBuffer, row = 0, column = 0, height = 0;
  if(!rowBytes) return;
  address = p.frameBuffer - p.frameBuffer % (Memory::PageSize % rowBytes ? rowBytes : Memory::PageSize);
  height = std::min<u32>(512, (Memory::VRAMSize - address) / rowBytes);
  row = (p.frameBuffer - address) / rowBytes, column = (p.frameBuffer - address) % rowBytes / bytes;
  bool depth = p.clear ? p.clearDepth : p.depthTest;
  if((p.frameBuffer - address) % rowBytes % bytes || column + std::min<s32>(region.right, p.stride - 1) >= p.stride ||
     row + region.bottom >= height || ((row || column) && depth)) {
    address = p.frameBuffer, row = 0, column = 0;
    height = std::min<u32>(512, (Memory::VRAMSize - address) / rowBytes);
  }
}

//(the bytes low to low + size - 1 of VRAM as a rectangle of one row)
static auto spanRect(u64 low, u64 size) -> GeModel::Rect {
  GeModel::Rect r;
  if(!size) return r;
  size = std::min<u64>(size, GeModel::VRAMBytes);
  r.address = u32(low % GeModel::VRAMBytes), r.rowBytes = GeModel::VRAMBytes, r.bytes = 1;
  r.left = 0, r.right = s32(size - 1), r.top = 0, r.bottom = 0;
  return r;
}

//(the 16 KiBs a put-back of VRAM's bytes first to last may reach: every depth buffer's whose rows meet them, all of
//its rows any PRIM has reached: putDepth() puts each one's newest depth whole)
static auto putBlocks(const GeModel& model, u64 first, u64 last, std::bitset<GeModel::Blocks>& covers) -> void {
  std::bitset<GeModel::Blocks> asked;
  for(u64 block = first >> 14; block <= last >> 14; block++) asked.set(block % GeModel::Blocks);
  for(auto& d : model.depthBuffers) {
    std::bitset<GeModel::Blocks> its;
    GeModel::depthBlocks(d.address, d.stride, d.rows, [&](u32 block) { its.set(block); });
    if((its & asked).any()) covers |= its;
  }
}

auto GE::modelPrimitive(GeModel& model, u32 word, u32 vertexAt, u32 indexAt, const GeThread::Copies& copies,
                        const u8* bytes, bool hardware, u64 at) -> bool {
  if(model.declares) model.interest = {};
  auto p = pixelState();
  auto format = vertexFormat();
  bool patching = word >> 24 != Primitive;
  u32 kind = word >> 16 & 7, count = word & 0xffff;
  if(!patching && (kind == 7 || !format.positionFormat)) return false;  //(draws nothing: primitive())
  bool textured = (commands[TextureMappingEnable] & 1) && !p.clear;
  Sampler texture = sampler();
  //(declaring: its texture's bytes, decoded or held before it's begun, whether or not it draws anything, and in VRAM
  //the rows of the targets with pages there; a put point's depth buffers and the targets over them)
  auto declareVRAM = [&](const std::bitset<GeModel::Pages>& pages) {
    model.interest.vram |= pages;
    model.targetsOver(pages, model.interest.vram);
  };
  auto declareBlocks = [&](const std::bitset<GeModel::Blocks>& blocks) {
    std::bitset<GeModel::Pages> pages;
    for(u32 block = 0; block < GeModel::Blocks; block++) {
      if(blocks[block]) for(u32 n = 0; n < 4; n++) pages.set(block * 4 + n);
    }
    declareVRAM(pages);
  };
  if(model.declares && textured) {
    u32 low, high, first, last;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    if(high > low && memory.pagesOf(low, high - low, first, last)) {
      u32 base = Memory::VRAMBase / Memory::PageSize;
      if(first >= base && last < base + GeModel::Pages) {
        std::bitset<GeModel::Pages> pages;
        for(u32 page = first; page <= last; page++) pages.set(page - base);
        declareVRAM(pages);
      } else {
        model.interest.first = first, model.interest.last = last;
      }
    }
  }
  //A put point first: a texture read through VRAM's depth views has the GPU's depth put into memory (holds()), with
  //a hardware renderer, as the PRIM begins.
  u32 physical = texture.address & 0x1fff'ffff;
  if(hardware && textured && texture.format <= 7 && physical - Memory::VRAMBase < Memory::VRAMWindow &&
     (physical - Memory::VRAMBase) / Memory::VRAMSize & 1) {
    static constexpr u32 bits[8] = {16, 16, 16, 32, 4, 8, 16, 32};
    u32 offset = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    u64 tall = (std::min<u32>(texture.height, 512) + 7) & ~7u;
    u64 end = std::min<u64>(offset + tall * texture.bufferWidth * bits[texture.format] / 8, Memory::VRAMSize);
    if(end > offset) {
      GeModel::PutPoint point{at, {}, {}};
      putBlocks(model, offset & ~0x3fffu, (end - 1) | 0x3fff, point.covers);
      for(u32 block = 0; block < GeModel::Blocks; block++) {
        if(point.covers[block]) point.before[block] = model.gpuDepth[block];
      }
      if(point.covers.any()) model.putPoints.push_back(point), model.dirtyings++;
      if(model.declares) declareBlocks(point.covers);
    }
  }
  //Where it may draw: the scissor rectangle, and in 2D, where its vertices reach (drawVertices(): the widest of
  //what each kind of primitive reaches between its outermost vertices)
  Region region{p.left, p.top, p.right, p.bottom};
  if(!patching && format.through && count && format.morphs == 1) {
    u32 indexBytes = format.indexFormat == 3 ? 4 : format.indexFormat;
    auto at8 = [&](u32 address, u32 size) -> const u8* {
      for(u32 n = 0; n < copies.ranges; n++) {
        const u8* base = bytes;
        for(u32 k = 0; k < n; k++) base += copies.size[k];
        u32 offset = address - copies.address[n];
        if(offset < copies.size[n] && size <= copies.size[n] - offset) return base + offset;
      }
      return nullptr;
    };
    auto number = [&](const u8* b) -> float {
      if(format.positionFormat == 1) return 0;  //(through mode's 8-bit positions read as 0: readTarget())
      if(format.positionFormat == 2) return float(s16(b[0] | b[1] << 8));
      u32 word = b[0] | b[1] << 8 | b[2] << 16 | u32(b[3]) << 24;
      float value;
      std::memcpy(&value, &word, 4);
      return value;
    };
    u32 size = format.positionFormat == 3 ? 4 : format.positionFormat;
    s32 minX = 65536, maxX = -65536, minY = 65536, maxY = -65536;
    bool known = true;
    for(u32 n = 0; n < count && known; n++) {
      u32 index = n;
      if(format.indexFormat) {
        const u8* i = at8(indexAt + n * indexBytes, indexBytes);
        if(!i) { known = false; break; }
        index = indexBytes == 1 ? i[0] : (i[0] | i[1] << 8);
      }
      const u8* v = at8(vertexAt + index * format.size + format.positionOffset, size * 2);
      if(!v) { known = false; break; }
      s32 x = fixed(number(v)), y = fixed(number(v + size));
      minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
    }
    if(known) {
      region.left = std::max(region.left, floorDivide(minX - 1, 16));
      region.right = std::min(region.right, floorDivide(maxX, 16));
      region.top = std::max(region.top, floorDivide(minY - 1, 16));
      region.bottom = std::min(region.bottom, floorDivide(maxY, 16));
    }
  }
  if(!p.stride || region.left > region.right || region.top > region.bottom || region.right < 0 || region.bottom < 0) {
    return false;  //(draws nothing: begin())
  }
  //The GPU's alone where its begin() surely takes it (the renderer there, the texture decoded from memory, the
  //target's rows enough); else the software renderer's too, which draws what the GPU refuses.
  u32 address, row, column, height;
  targetPlace(p, region, address, row, column, height);
  //(declaring: its target's rows, the target begin() picks from the PRIM's own region being this one or the frame
  //buffer's (targetPlace()'s two), and the targets over them; where it tests or clears depth, its depth buffer's 16
  //KiBs down to the most rows any PRIM over it has reached, this one's too (R24), and the targets over those (R22))
  if(model.declares && hardware) {
    u32 rowBytes = p.stride * (p.format == 3 ? 4 : 2);
    u32 aligned = p.frameBuffer - p.frameBuffer % (Memory::PageSize % rowBytes ? rowBytes : Memory::PageSize);
    std::bitset<GeModel::Pages> pages;
    for(u32 start : {aligned, p.frameBuffer}) {
      GeModel::KnownTarget t{start, p.stride, p.format};
      model.knowTarget(start, p.stride, p.format);
      GeModel::targetRows(t, pages);
    }
    declareVRAM(pages);
    if((p.clear ? p.clearDepth : p.depthTest) && p.depthStride) {
      std::bitset<GeModel::Blocks> blocks;
      u32 rows = std::max<u32>(model.depthBuffer(p.depthBuffer, p.depthStride).rows, region.bottom + 1);
      GeModel::depthBlocks(p.depthBuffer, p.depthStride, rows, [&](u32 block) { blocks.set(block); });
      declareBlocks(blocks);
    }
  }
  bool gpu = hardware, certain = hardware && !model.unsure && height && region.bottom + s32(row) < s32(height);
  if(certain && textured) {
    u32 low, high;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    certain = texture.format <= 10 && memory.reaches(low, high - low);
  }
  bool software = !certain;
  u32 bytesEach = p.format == 3 ? 4 : 2, rowBytes = p.stride * bytesEach;
  bool depth = p.clear ? p.clearDepth : p.depthTest;
  bool depthWrites = depth && (p.clear || p.depthWrite);
  //(its depth's 16 KiBs, in the rows it draws)
  std::bitset<GeModel::Blocks> depthBlocks;
  if(depthWrites && p.depthStride) {
    u64 low = (u64(p.depthBuffer) + (u64(std::max(region.top, 0)) * p.depthStride + std::max(region.left, 0)) * 2);
    u64 high = u64(p.depthBuffer) + (u64(region.bottom) * p.depthStride + std::max(region.right, 0)) * 2 + 1;
    for(u64 block = low >> 14; block <= high >> 14 && block < (low >> 14) + GeModel::Blocks; block++) {
      depthBlocks.set(block % GeModel::Blocks);
    }
  }
  if(gpu) {
    GeModel::Rect r;
    r.address = p.frameBuffer, r.rowBytes = rowBytes, r.bytes = bytesEach;
    r.left = std::max(region.left, 0), r.top = std::max(region.top, 0);
    r.right = std::min<s32>(region.right, s32(p.stride) - 1 - s32(column)), r.bottom = region.bottom;
    if(model.scaled) {  //(above 1x a pixel's read back may take its neighbour's sample, up and left: a pixel more)
      r.left = std::max(r.left - 1, 0), r.top = std::max(r.top - 1, 0);
      r.right = std::min<s32>(r.right + 1, s32(p.stride) - 1 - s32(column));
      if(height > row) r.bottom = std::min<s32>(r.bottom + 1, s32(height - row) - 1);
    }
    model.gpu.add(r, at);
    //a depth buffer's newest GPU depth writer of the other order: its depth may be taken through memory
    //(takeDepth()), a put point over the depth buffers there
    if(depth && p.depthStride) {
      auto& d = model.depthBuffer(p.depthBuffer, p.depthStride);
      d.rows = std::max<u32>(d.rows, region.bottom + 1);
      u32 mine = Memory::depthCopy(p.format) == 3 ? 2 : 1;
      if(d.newestClass != ~0u && (d.newestClass & ~mine)) {
        GeModel::PutPoint point{at, {}, {}};
        u64 first = d.address, last = u64(d.address) + u64(d.rows) * d.stride * 2 - 1;
        putBlocks(model, first, last, point.covers);
        for(u32 block = 0; block < GeModel::Blocks; block++) {
          if(point.covers[block]) point.before[block] = model.gpuDepth[block];
        }
        model.putPoints.push_back(point), model.dirtyings++;
        if(model.declares) declareBlocks(point.covers);
      }
      if(depthWrites) d.newestClass = certain ? mine : (d.newestClass == ~0u ? 0 : d.newestClass) | mine;
    }
    if(depthBlocks.any()) {  //(none for most PRIMs: 128 looked at for each)
      for(u32 block = 0; block < GeModel::Blocks; block++) {
        if(depthBlocks[block]) model.gpuDepth[block] = at;
      }
    }
    //above 1x, its target's hull of rectangles since the last finish, other targets' hulls in the pages its rows
    //reach merged in (begin()'s moves)
    if(model.scaled) {
      GeModel::Target* t = nullptr;
      for(auto& candidate : model.targets) {
        if(candidate.address == address && candidate.stride == p.stride && candidate.format == p.format) t = &candidate;
      }
      if(!t) t = &model.targets.emplace_back(GeModel::Target{address, p.stride, p.format, {}, 0});
      GeModel::Rect h;
      h.address = address, h.rowBytes = rowBytes, h.bytes = bytesEach;
      h.left = r.left + column, h.top = r.top + row, h.right = r.right + column, h.bottom = r.bottom + row;
      auto merge = [&](const GeModel::Rect& more) {
        if(t->hullAt <= model.finishKnown || t->hull.empty()) return void(t->hull = more);
        auto& m = t->hull;
        m.left = std::min(m.left, more.left), m.top = std::min(m.top, more.top);
        m.right = std::max(m.right, more.right), m.bottom = std::max(m.bottom, more.bottom);
      };
      merge(h);
      u64 low = address, high = u64(address) + u64(region.bottom + row + 1) * rowBytes - 1;
      for(auto& o : model.targets) {
        if(&o == t || o.hullAt <= model.finishKnown || o.hull.empty()) continue;
        u64 oLow = o.hull.low(), oHigh = oLow + o.hull.span() - 1;
        if(oHigh / 4096 < low / 4096 || oLow / 4096 > high / 4096) continue;
        GeModel::Rect rows;  //(whole rows of this one over the other's)
        rows.address = address, rows.rowBytes = rowBytes, rows.bytes = bytesEach;
        rows.left = 0, rows.right = p.stride - 1;
        rows.top = oLow > address ? s32((oLow - address) / rowBytes) : 0;
        rows.bottom = s32((oHigh - address) / rowBytes);
        merge(rows);
        t->hullAt = std::max(t->hullAt, o.hullAt);
      }
      t->hullAt = std::max(t->hullAt, at);
      model.dirtyings++;
    }
  }
  if(software && textured) {  //(its texture, which the software renderer may read from memory as it draws)
    u32 low, high, first, last;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    if(high > low && memory.pagesOf(low, high - low, first, last)) {
      for(u32 page = first; page <= last && page < model.softwareTextures.size(); page++) {
        model.softwareTextures[page] = at;
      }
      model.softwareTexturesNewest = at;
    }
  }
  if(software) {
    //(its pixels may run on past a row's end into the next: as one run from its first to its last)
    u64 low = u64(p.frameBuffer) + (u64(std::max(region.top, 0)) * p.stride + std::max(region.left, 0)) * bytesEach;
    u64 high = u64(p.frameBuffer) + (u64(region.bottom) * p.stride + std::max(region.right, 0) + 1) * bytesEach - 1;
    model.software.add(spanRect(low, high - low + 1), at);
    if(depthBlocks.any()) {
      for(u32 block = 0; block < GeModel::Blocks; block++) {
        if(depthBlocks[block]) model.software.add(spanRect(u64(block) << 14, 0x4000), at);
      }
    }
  }
  return certain;
}
