//Vertices: VERTEX_TYPE says which parts a vertex has and how each is stored; the GE reads them from memory as PRIM
//asks. In memory a vertex's parts come in this order: weights (for skinning), texture coordinates, color, normal,
//position. The vertex type's fields (pspgu.h's GU_TEXTURE_8BIT and the rest):
//  bits 0-1    texture coordinates: 0 none, 1 8-bit, 2 16-bit, 3 float
//  bits 2-4    color: 0 none, 4 5650, 5 5551, 6 4444, 7 8888
//  bits 5-6    normal, bits 7-8 position, bits 9-10 weights: as the texture coordinates
//  bits 11-12  indices: 0 none (vertices in order), 1 8-bit, 2 16-bit
//  bits 14-16  how many weights, less one; bits 18-20 how many morph targets, less one
//  bit 23      through mode: 2D, positions already in screen pixels and nothing transformed
//With morph targets, a vertex holds each target's parts one after another (the GE blends them: readVertex).

//The size of an 8-bit, 16-bit or float number: 1, 2 or 4 bytes (0 for none).
static auto numberSize(u32 format) -> u32 { return format == 3 ? 4 : format; }

static auto alignUp(u32 offset, u32 size) -> u32 { return size ? (offset + size - 1) / size * size : offset; }

auto GE::vertexFormat() const -> VertexFormat {
  u32 type = commands[VertexType];
  VertexFormat f{};
  f.textureFormat  = type >>  0 & 3;
  f.colorFormat    = type >>  2 & 7;
  f.normalFormat   = type >>  5 & 3;
  f.positionFormat = type >>  7 & 3;
  f.weightFormat   = type >>  9 & 3;
  f.indexFormat    = type >> 11 & 3;
  f.weights        = (type >> 14 & 7) + 1;
  f.morphs         = (type >> 18 & 7) + 1;
  f.through        = type >> 23 & 1;
  if(f.colorFormat && f.colorFormat < 4) f.colorFormat = 0;  //1-3 aren't color formats: no color

  u32 offset = 0, largest = 1;
  auto place = [&](u32 size, u32 count) -> u32 {  //a part of count numbers, each of size bytes
    offset = alignUp(offset, size);
    u32 at = offset;
    offset += size * count;
    largest = std::max(largest, size);
    return at;
  };
  if(f.weightFormat)   f.weightOffset   = place(numberSize(f.weightFormat), f.weights);
  if(f.textureFormat)  f.textureOffset  = place(numberSize(f.textureFormat), 2);
  if(f.colorFormat)    f.colorOffset    = place(f.colorFormat == 7 ? 4 : 2, 1);
  if(f.normalFormat)   f.normalOffset   = place(numberSize(f.normalFormat), 3);
  if(f.positionFormat) f.positionOffset = place(numberSize(f.positionFormat), 3);
  f.size = alignUp(offset, largest) * f.morphs;
  return f;
}

//A number of format (8-bit, 16-bit or float) at address, as a float: signed or not as asked.
static auto readNumber(Memory& memory, u32 address, u32 format, bool isSigned) -> float {
  if(format == 1) return isSigned ? float(s8(memory.read(1, address))) : float(memory.read(1, address));
  if(format == 2) return isSigned ? float(s16(memory.read(2, address))) : float(memory.read(2, address));
  u32 bits = memory.read(4, address);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

//A color in one of the vertex formats, as 8888 with red in the low byte. Narrow fields widen by repeating their top
//bits, so the brightest value stays the brightest (31 becomes 255, not 248).
static auto widen(u32 value, u32 bits) -> u32 { return value << (8 - bits) | value >> (2 * bits - 8); }
static auto readColor(Memory& memory, u32 address, u32 format) -> u32 {
  if(format == 7) return memory.read(4, address);
  u32 c = memory.read(2, address), r, g, b, a;
  if(format == 4) r = widen(c & 31, 5), g = widen(c >> 5 & 63, 6), b = widen(c >> 11 & 31, 5), a = 255;
  else if(format == 5) r = widen(c & 31, 5), g = widen(c >> 5 & 31, 5), b = widen(c >> 10 & 31, 5), a = c >> 15 ? 255 : 0;
  else r = widen(c & 15, 4), g = widen(c >> 4 & 15, 4), b = widen(c >> 8 & 15, 4), a = widen(c >> 12, 4);
  return r | g << 8 | b << 16 | a << 24;
}

//In 3D an 8-bit number counts 128ths and a 16-bit one 32768ths, so that the largest are nearly 1 (a signed 16-bit
//32767 is 0.99997); floats are as they are. Through mode takes numbers as stored.
static auto unit(u32 format, bool through) -> float {
  if(through) return 1.0f;
  return format == 1 ? 1.0f / 128 : format == 2 ? 1.0f / 32768 : 1.0f;
}

//One morph target of the vertex at address (or the vertex itself, with none). Positions are signed, except a
//through-mode depth, which runs 0-65535 (that 8- and 16-bit depths are unsigned in through mode is as PPSSPP has it);
//texture coordinates and weights are unsigned; normals signed.
static auto readTarget(Memory& memory, u32 address, const GE::VertexFormat& f) -> GE::Vertex {
  GE::Vertex vertex;
  if(f.weightFormat) {
    float scale = unit(f.weightFormat, f.through);
    for(u32 n = 0; n < f.weights; n++) {
      u32 at = address + f.weightOffset + n * numberSize(f.weightFormat);
      vertex.weights[n] = readNumber(memory, at, f.weightFormat, false) * scale;
    }
  }
  if(f.textureFormat) {
    u32 size = numberSize(f.textureFormat);
    float scale = unit(f.textureFormat, f.through);
    vertex.u = readNumber(memory, address + f.textureOffset, f.textureFormat, false) * scale;
    vertex.v = readNumber(memory, address + f.textureOffset + size, f.textureFormat, false) * scale;
  }
  if(f.colorFormat) vertex.color = readColor(memory, address + f.colorOffset, f.colorFormat);
  if(f.normalFormat) {
    u32 size = numberSize(f.normalFormat);
    float scale = unit(f.normalFormat, f.through);
    for(u32 n = 0; n < 3; n++) {
      vertex.normal[n] = readNumber(memory, address + f.normalOffset + n * size, f.normalFormat, true) * scale;
    }
  }
  if(f.positionFormat) {
    u32 size = numberSize(f.positionFormat), at = address + f.positionOffset;
    float scale = unit(f.positionFormat, f.through);
    vertex.x = readNumber(memory, at, f.positionFormat, true) * scale;
    vertex.y = readNumber(memory, at + size, f.positionFormat, true) * scale;
    vertex.z = readNumber(memory, at + size * 2, f.positionFormat, !f.through) * scale;
  }
  return vertex;
}

//The vertex at address. With morph targets (a vertex type with more than one) it's their sum, each part of each
//target weighted by its MORPH_WEIGHT; colors channel by channel, cut to whole numbers and held to 0-255 (as PPSSPP
//has it).
auto GE::readVertex(u32 address, const VertexFormat& f) -> Vertex {
  if(f.morphs == 1) return readTarget(memory, address, f);
  Vertex sum;
  float color[4] = {};
  for(u32 n = 0; n < f.morphs; n++) {
    float weight = float24(commands[MorphWeight0 + n]);
    Vertex target = readTarget(memory, address + n * (f.size / f.morphs), f);
    for(u32 k = 0; k < 8; k++) sum.weights[k] += target.weights[k] * weight;
    sum.u += target.u * weight, sum.v += target.v * weight;
    for(u32 k = 0; k < 4; k++) color[k] += float(target.color >> k * 8 & 0xff) * weight;
    for(u32 k = 0; k < 3; k++) sum.normal[k] += target.normal[k] * weight;
    sum.x += target.x * weight, sum.y += target.y * weight, sum.z += target.z * weight;
  }
  for(u32 k = 0; k < 4; k++) {
    s32 value = color[k] > 0 ? s32(std::min(color[k], 255.0f)) : 0;
    sum.color |= u32(value) << k * 8;
  }
  return sum;
}

//The n-th index of an indexed PRIM: which vertex, counted from the vertex address.
auto GE::readIndex(u32 n, const VertexFormat& f) -> u32 {
  if(f.indexFormat == 1) return memory.read(1, indexAddress + n);
  if(f.indexFormat == 2) return memory.read(2, indexAddress + n * 2);
  note("an index format the PSP doesn't have (3): vertices taken in order");
  return n;
}
