//Textures (ares/psp/ge/texture.cpp, the same arithmetic): which texels a coordinate falls between, the texel or the
//four filtered, and the texture function. Textures arrive decoded (8888, exactly as the software renderer's decoded
//copies hold them), so a texel is one word of texels.

//A texture coordinate as sampling takes it (texelAxis()): x the texel it's in (nearest), or x and y the two whose
//middles it lies between and z how far from the first to the second in sixteenths (filtered), inside the texture
//(repeated, or held at the edge). size and clamped: the texture's width and its wrap across, or its height and
//its wrap down.
int insideTexture(int c, uint size, bool clamped) {
  int last = int(min(size, 512u)) - 1;
  return clamped ? clamp(c, 0, last) : c & last;
}
ivec3 texelAxis(float coordinate, uint size, bool clamped, bool linear) {
  //(a GPU may take a number below the normal floats for zero, where the host's floor() of a negative one is -1: a
  //normal number as small to it, 2^-100 of its sign, floors as that one does, scaled or not)
  uint bits = floatBitsToUint(coordinate);
  if((bits & 0x7f800000u) == 0u && (bits & 0x007fffffu) != 0u) {
    coordinate = uintBitsToFloat((bits & 0x80000000u) | 0x0d800000u);
  }
  precise float held = notANumber(coordinate) ? 0.0 : clamp(coordinate, -65536.0, 65536.0);
  if(!linear) return ivec3(insideTexture(int(floor(held)), size, clamped), 0, 0);
  precise float scaled = held * 256.0;
  int base = int(floor(scaled)) - 128;
  int first = base >> 8;
  return ivec3(insideTexture(first, size, clamped), insideTexture(first + 1, size, clamped), base >> 4 & 15);
}
//(a sprite's axes, worked out on the CPU, come in a word: first, second << 10, fraction << 20)
ivec3 unpackAxis(uint word) { return ivec3(int(word & 0x3ffu), int(word >> 10 & 0x3ffu), int(word >> 20 & 15u)); }

//The texel at (x, y), inside the texture. (The rows a primitive may reach are all there: draw.cpp worked them out.
//The index is held inside the texture's copy all the same, so that nothing reads past it.)
uint fetch(uint look, int x, int y) {
  uint width = lookWord(look, LookTexelWidth), rows = lookWord(look, LookTexelRows);
  uint at = min(uint(y), rows - 1u) * width + min(uint(x), width - 1u);
  return texels[lookWord(look, LookTexels) + at];
}

//The four texels around (u, v) blended: the top two, then the bottom two, then those two results, each step
//dropping its fraction (texture.cpp's filtered(), a channel at a time).
uint filtered(uint look, ivec3 u, ivec3 v) {
  uint topLeft = fetch(look, u.x, v.x), topRight = fetch(look, u.y, v.x);
  uint bottomLeft = fetch(look, u.x, v.y), bottomRight = fetch(look, u.y, v.y);
  int across = u.z, down = v.z;
  uint result = 0u;
  for(int n = 0; n < 4; n++) {
    int upper = (channel(topLeft, n) * (16 - across) + channel(topRight, n) * across) >> 4;
    int lower = (channel(bottomLeft, n) * (16 - across) + channel(bottomRight, n) * across) >> 4;
    result |= uint((upper * (16 - down) + lower * down) >> 4) << uint(n * 8);
  }
  return result;
}

uint sampleAxes(uint look, bool linear, ivec3 u, ivec3 v) {
  return linear ? filtered(look, u, v) : fetch(look, u.x, v.x);
}
//The texture at (u, v), in texels (texture.cpp's sampleWith()).
uint sampleAt(uint look, bool linear, float u, float v) {
  uint flags = lookWord(look, LookFlags);
  ivec3 across = texelAxis(u, lookWord(look, LookWidth), (flags & FlagClampU) != 0u, linear);
  ivec3 down = texelAxis(v, lookWord(look, LookHeight), (flags & FlagClampV) != 0u, linear);
  return sampleAxes(look, linear, across, down);
}

//The texture function (texture.cpp's textureFunctionWith()): how the texel and the pixel's own color combine.
//modulate, decal, blend (with the environment color), replace, add (and 5-7, as add); the texture's alpha or not;
//color doubling. Products are (Cf + 1) * Ct / 256, but blend's, which rounds up.
//(a channel at a time, each its own call: see pixel.glsl's blendChannel())
int combineChannel(uint function, bool withAlpha, bool doubled, int f, int t, int texelAlpha, int environment) {
  int twice = doubled ? 2 : 1, down = doubled ? 7 : 8;
  if(function == 0u) return (f + 1) * t * twice >> 8;
  if(function == 1u && withAlpha) return ((f + 1) * (255 - texelAlpha) + (t + 1) * texelAlpha) >> down;
  if(function == 1u) return t * twice;
  if(function == 2u) return ((255 - t) * f + t * environment + 255) >> down;
  if(function == 3u) return t * twice;
  return (f + t) * twice;
}
uint combine(uint look, uint color, uint texel) {
  uint flags = lookWord(look, LookFlags);
  uint function = lookWord(look, LookFunction), environment = lookWord(look, LookEnvironment);
  bool withAlpha = (flags & FlagWithAlpha) != 0u, doubled = (flags & FlagDoubled) != 0u;
  int fragmentAlpha = channel(color, 3), texelAlpha = channel(texel, 3);
  int alpha = withAlpha ? (fragmentAlpha + 1) * texelAlpha >> 8 : fragmentAlpha;
  if(function == 1u) alpha = fragmentAlpha;
  if(function == 3u) alpha = withAlpha ? texelAlpha : fragmentAlpha;
  return pack(combineChannel(function, withAlpha, doubled, channel(color, 0), channel(texel, 0), texelAlpha,
                             channel(environment, 0)),
              combineChannel(function, withAlpha, doubled, channel(color, 1), channel(texel, 1), texelAlpha,
                             channel(environment, 1)),
              combineChannel(function, withAlpha, doubled, channel(color, 2), channel(texel, 2), texelAlpha,
                             channel(environment, 2)),
              alpha);
}

//lighting's shine, kept apart, added after the texture (raster.cpp's shadeAs())
uint addShine(uint color, uint specular) {
  if(specular == 0u) return color;
  return pack(channel(color, 0) + channel(specular, 0), channel(color, 1) + channel(specular, 1),
              channel(color, 2) + channel(specular, 2), channel(color, 3));
}
