//Vertices: VERTEX_TYPE says which parts a vertex has and how each is stored; the GE reads them from memory as PRIM
//asks. In memory a vertex's parts come in this order: weights (for skinning), texture coordinates, color, normal,
//position. The vertex type's fields (pspgu.h's GU_TEXTURE_8BIT and the rest):
//  bits 0-1    texture coordinates: 0 none, 1 8-bit, 2 16-bit, 3 float
//  bits 2-4    color: 0 none, 4 5650, 5 5551, 6 4444, 7 8888
//  bits 5-6    normal, bits 7-8 position, bits 9-10 weights: as the texture coordinates
//  bits 11-12  indices: 0 none (vertices in order), 1 8-bit, 2 16-bit, 3 32-bit (readIndex())
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

//GCC only: a float vertex number's bits as a float, a signaling not-a-number quieted (the fraction's top bit set) as
//the host's float operations quiet one. The readers' multiplication by the number's unit, 1 for a float, is what
//quiets it, and GCC at -O3 drops it where it sees the 1 (in runs), its default -fno-signaling-nans allowing that, so
//under GCC it's done here in whole numbers, which no optimizer may drop. Clang keeps the multiplication in both
//readers, so its builds (the app's, on Android and the Mac) do without this and pay nothing for it;
//tests/psp/ge.cpp's "ge vertex floats that are signaling NaNs read quieted" fails on a clang that drops it.
#if defined(__GNUC__) && !defined(__clang__)
static alwaysinline auto vertexFloat(u32 bits) -> float {
  bits |= u32((bits << 1) > 0xff00'0000) << 22;  //(not a number: exponent all ones, fraction not 0; either sign)
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}
#endif

//A number of format (8-bit, 16-bit or float) at address, as a float: signed or not as asked. read(size, address)
//reads memory (readVertex() says how).
template<typename Read>
static auto readNumber(const Read& read, u32 address, u32 format, bool isSigned) -> float {
  if(format == 1) return isSigned ? float(s8(read(1, address))) : float(read(1, address));
  if(format == 2) return isSigned ? float(s16(read(2, address))) : float(read(2, address));
  #if defined(__GNUC__) && !defined(__clang__)
  return vertexFloat(read(4, address));
  #else
  u32 bits = read(4, address);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
  #endif
}

//A color in one of the vertex formats, as 8888 with red in the low byte. Narrow fields widen by repeating their top
//bits, so the brightest value stays the brightest (31 becomes 255, not 248).
static auto widen(u32 value, u32 bits) -> u32 { return value << (8 - bits) | value >> (2 * bits - 8); }
template<typename Read>
static auto readColor(const Read& read, u32 address, u32 format) -> u32 {
  if(format == 7) return read(4, address);
  u32 c = read(2, address), r, g, b, a;
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
template<typename Read>
static auto readTarget(const Read& memory, u32 address, const GE::VertexFormat& f) -> GE::Vertex {
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
  //(8-bit positions in through mode read as 0: pspautotests' gpu/primitives programs recorded a PSP drawing their two
  //points at (0, 0), and nothing of their triangles, lines and sprites; that their depth is 0 too isn't measured)
  if(f.positionFormat && !(f.through && f.positionFormat == 1)) {
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
//has it). Its bytes are read straight from where they are in the host's memory when they're all in one place there
//(the same bytes memory.read() would give), else through memory.read() one number at a time.
auto GE::readVertex(u32 address, const VertexFormat& f) -> Vertex {
  if(const u8* base = f.size ? memory.pointer(address, f.size) : nullptr) {
    return readVertexWith([&](u32 size, u32 at) -> u32 {
      const u8* bytes = base + (at - address);
      if(size == 1) return bytes[0];
      if(size == 2) return bytes[0] | bytes[1] << 8;
      return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | u32(bytes[3]) << 24;
    }, address, f);
  }
  return readVertexWith([&](u32 size, u32 at) { return memory.read(size, at); }, address, f);
}

template<typename Read>
auto GE::readVertexWith(const Read& memory, u32 address, const VertexFormat& f) -> Vertex {
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

//A number of format (8-bit, 16-bit or float) from the host's bytes, as readNumber() reads it: the reader byFormat()
//hands its body, made for the format, so that a loop over many vertices doesn't look at the format for each number.
template<typename Body> static alwaysinline auto byFormat(u32 format, bool isSigned, const Body& body) -> void {
  switch(format | u32(isSigned) << 2) {
  case 1: return body([](const u8* at) { return float(at[0]); });
  case 5: return body([](const u8* at) { return float(s8(at[0])); });
  case 2: return body([](const u8* at) { u16 value; std::memcpy(&value, at, 2); return float(value); });
  case 6: return body([](const u8* at) { u16 value; std::memcpy(&value, at, 2); return float(s16(value)); });
  #if defined(__GNUC__) && !defined(__clang__)
  case 3: case 7: return body([](const u8* at) { u32 bits; std::memcpy(&bits, at, 4); return vertexFloat(bits); });
  #else
  case 3: case 7: return body([](const u8* at) { float value; std::memcpy(&value, at, 4); return value; });
  #endif
  }
}

//count vertices without morph targets into out, which holds count Vertex{}: the n-th from the host's bytes at bytes
//+ offsets[n], all of its bytes there, as readTarget() reads it (through readVertex(), which takes them the same way),
//but a part at a time over all of them, each loop made for its part's format. (Army of Two reads 650,000 vertices a
//frame, a hundred or so to a PRIM.)
auto GE::readVerticesFrom(const u8* bytes, const u32* offsets, u32 count, const VertexFormat& f, Vertex* out) -> void {
  if(f.weightFormat) {
    float scale = unit(f.weightFormat, f.through);
    u32 size = numberSize(f.weightFormat);
    byFormat(f.weightFormat, false, [&](auto number) {
      for(u32 n = 0; n < count; n++) {
        const u8* at = bytes + offsets[n] + f.weightOffset;
        for(u32 k = 0; k < f.weights; k++) out[n].weights[k] = number(at + k * size) * scale;
      }
    });
  }
  if(f.textureFormat) {
    float scale = unit(f.textureFormat, f.through);
    u32 size = numberSize(f.textureFormat);
    byFormat(f.textureFormat, false, [&](auto number) {
      for(u32 n = 0; n < count; n++) {
        const u8* at = bytes + offsets[n] + f.textureOffset;
        out[n].u = number(at) * scale;
        out[n].v = number(at + size) * scale;
      }
    });
  }
  if(f.colorFormat) {
    auto colors = [&](auto color) {
      for(u32 n = 0; n < count; n++) out[n].color = color(bytes + offsets[n] + f.colorOffset);
    };
    auto read = [](u32 size, const u8* at) -> u32 {
      u32 value = 0;
      std::memcpy(&value, at, size);
      return value;
    };
    switch(f.colorFormat) {
    case 7: colors([&](const u8* at) { return read(4, at); }); break;
    case 4: colors([&](const u8* at) { return readColor([&](u32 size, u32) { return read(size, at); }, 0, 4); }); break;
    case 5: colors([&](const u8* at) { return readColor([&](u32 size, u32) { return read(size, at); }, 0, 5); }); break;
    default: colors([&](const u8* at) { return readColor([&](u32 size, u32) { return read(size, at); }, 0, 6); });
    }
  }
  if(f.normalFormat) {
    float scale = unit(f.normalFormat, f.through);
    u32 size = numberSize(f.normalFormat);
    byFormat(f.normalFormat, true, [&](auto number) {
      for(u32 n = 0; n < count; n++) {
        const u8* at = bytes + offsets[n] + f.normalOffset;
        for(u32 k = 0; k < 3; k++) out[n].normal[k] = number(at + k * size) * scale;
      }
    });
  }
  if(f.positionFormat && !(f.through && f.positionFormat == 1)) {  //(as readTarget(): through mode's 8-bit are 0)
    float scale = unit(f.positionFormat, f.through);
    u32 size = numberSize(f.positionFormat);
    byFormat(f.positionFormat, true, [&](auto number) {
      byFormat(f.positionFormat, !f.through, [&](auto depth) {
        for(u32 n = 0; n < count; n++) {
          const u8* at = bytes + offsets[n] + f.positionOffset;
          out[n].x = number(at) * scale;
          out[n].y = number(at + size) * scale;
          out[n].z = depth(at + size * 2) * scale;
        }
      });
    });
  }
}

//The n-th index of an indexed PRIM (or BOUNDING_BOX): which vertex, counted from the vertex address. Format 3, which
//pspgu.h doesn't name, holds 32-bit indices of which the GE takes the low 16 bits, as pspautotests recorded on a PSP:
//gpu/primitives/indices drew with them as with the others, indices32 drew 0x10000 as 0 and 0xffff0000 as 0, and
//gpu/bounding/vertexaddr found the index address moved on 4 bytes for each.
auto GE::readIndex(u32 n, const VertexFormat& f) -> u32 {
  if(f.indexFormat == 1) return memory.read(1, indexAddress + n);
  if(f.indexFormat == 2) return memory.read(2, indexAddress + n * 2);
  return memory.read(4, indexAddress + n * 4) & 0xffff;
}
