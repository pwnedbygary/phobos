//sceJpeg: Motion JPEG pictures decoded on the Media Engine, and YCbCr pictures converted to pixels. Games decode a
//movie's or a menu's JPEG pictures with it, or convert the YCbCr pictures sceMpeg gives (Monster Hunter Portable 3rd
//converts its movies' pictures with sceJpegCsc, from sceMpegAvcConvertToYuv420's).
//
//What each function does is as pspautotests' jpeg tests recorded on a PSP (init, create, delete, finish,
//getoutputinfo, decode, decodes, decodeycbcr, decodeycbcrs, csc and mjpegcsc), with pspsdk's pspjpeg.h for the
//arguments and the errors it names:
//  - The decoder is the library's one context: sceJpegInitMJpeg starts the library (again: refused), sceJpegCreateMJpeg
//    gives the context the size of the picture buffer pictures are decoded into, up to 1024 pixels wide (any height),
//    sceJpegDeleteMJpeg takes it away, and sceJpegFinishMJpeg ends the library, refused while a context is made. The
//    size stays after the context is deleted, and decoding into pixels still uses it (decode's "After delete").
//  - The pictures: baseline JPEG (Huffman coded, 8 bits), Y, Cb and Cr with the colours at half the picture's size
//    each way (4:2:0); a grey picture, or colours at half its width only, is refused (UNSUPPORT_SAMPLING). Decoded
//    into YCbCr, a picture is its three planes one after another, each a row after row as wide as it is: Y, then Cb,
//    then Cr (480x272: 195840 bytes, what sceJpegGetOutputInfo says it needs); into pixels, 32-bit ones, a row of the
//    picture to a row of the context's width. Both return the picture's width and height (width << 16 | height).
//  - The YCbCr conversions: sceJpegCsc (any time) and sceJpegMJpegCsc (the library started), from those planes into
//    32-bit pixels, a row of the picture to a row of the buffer's width. Their sums are fitted to csc's and mjpegcsc's
//    recorded pixels: sceJpegCsc's is JPEG's own (JFIF: Y, Cb and Cr all 0-255) in 16-bit fixed point, each colour
//    rounded to the nearest; sceJpegMJpegCsc's has coefficients of 8 bits only and its Cb and Cr reach 1 at least
//    (Cb or Cr 0 and 1 give the same pixel, and Cr 254 and 255 the same green). Alpha is 0. Decoding into pixels is
//    the Media Engine's conversion, chosen to be sceJpegMJpegCsc's sums with its rows written as they are (decode's
//    recordings show no pixel, and no buffer off a multiple of 8 bytes).
//
//The decoder is Phobos's own, from ITU-T T.81 (JPEG's standard): its markers, Huffman tables (Annex C, and Annex K's
//usual ones for a picture that has none, as Motion JPEG's pictures may not), zigzag order and inverse DCT (A.3.3),
//in floating point and rounded to the nearest. The Media Engine's own rounding isn't recorded, so a pixel may differ
//from a PSP's by one.
//
//Not recorded, so chosen: data that ends part way through a picture decodes the rest as zeros; a Huffman code that
//isn't in its table counts as 0; markers other than JPEG's baseline and extended sequential ones (progressive,
//lossless, arithmetic coding) are UNKNOWN_MARKER. Not here: the library's two functions no game here calls and no
//recording describes (sceJpegDecompressAllImage, sceJpeg_9B36444C). Differences from the recordings, all with
//arguments outside what games pass: decodeycbcr's into a null buffer wrote somewhere the test could see (nothing here);
//csc's colour information with its top 12 bits set wrote past the picture; mjpegcsc's pictures under 16 pixels wide
//or 16 high, and its negative stride's rows past the eighth, wrote what they don't here.

namespace {

enum : u32 {
  JpegBadMarkerLength       = 0x8065'0004,  //pspjpeg.h's: a segment past the data's end, or no data at all
  JpegInvalidPointer        = 0x8065'0010,  //pspjpeg.h's
  JpegUnsupportedColorspace = 0x8065'0013,  //pspjpeg.h's
  JpegUnsupportedSampling   = 0x8065'0016,  //pspjpeg.h's
  JpegUnsupportedImageSize  = 0x8065'0020,  //pspjpeg.h's
  JpegNotJpeg               = 0x8065'0023,  //(recorded) data that doesn't start with SOI
  JpegUnknownMarker         = 0x8065'0035,  //pspjpeg.h's
  JpegWrongState            = 0x8065'0039,  //(recorded) the library or its context not as the call needs
  JpegBufferTooSmall        = 0x8065'0041,  //(recorded) a YCbCr buffer smaller than the picture
  JpegAlreadyInitialized    = 0x8065'0042,  //(recorded)
  JpegNullBuffer            = 0x8065'0051,  //(recorded)
};

//A Huffman table (T.81's Annex C: codes made from how many there are of each length, F.16's decoding procedure)
struct JpegTable {
  bool defined = false;
  u8 values[256] = {};
  s32 minCode[17] = {}, maxCode[17] = {}, first[17] = {};

  //counts: how many codes of each length, 1 to 16; false if they don't make a table
  auto build(const u8 counts[16], const u8* symbols) -> bool {
    u32 total = 0, code = 0;
    for(u32 length = 1; length <= 16; length++) {
      u32 count = counts[length - 1];
      first[length] = total;
      minCode[length] = code;
      maxCode[length] = count ? s32(code + count - 1) : -1;
      code += count;
      total += count;
      if(total > 256 || code > 1u << length) return false;
      code <<= 1;
    }
    memcpy(values, symbols, total);
    return defined = true;
  }
};

//Annex K's tables (K.3 to K.6): what Motion JPEG's pictures without tables of their own are coded with
const u8 jpegLumaDCCounts[16]   = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const u8 jpegChromaDCCounts[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const u8 jpegDCValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const u8 jpegLumaACCounts[16]   = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const u8 jpegLumaACValues[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa};
const u8 jpegChromaACCounts[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const u8 jpegChromaACValues[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa};

//A JPEG picture: its markers read (header()), then its scan decoded into a plane for each component (decode())
struct JpegPicture {
  struct Component {
    u32 id = 0, h = 1, v = 1, quantizer = 0, dc = 0, ac = 0;
    s32 prediction = 0;
    u32 stride = 0;         //its plane's width, whole blocks for whole MCUs
    std::vector<u8> plane;
  };

  Memory* memory = nullptr;
  u32 address = 0, reached = 0;  //the picture's data: as much of its size as memory reaches
  std::vector<u8> bytes;         //what of it has been read
  const u8* data = nullptr;
  u32 at = 0;
  u16 quantizers[4][64] = {};
  bool quantizerDefined[4] = {};
  JpegTable dcTables[4], acTables[4];
  u32 restartInterval = 0;
  u32 width = 0, height = 0;
  Component components[3];
  u32 componentCount = 0;
  bool frame = false;  //SOF read
  //the scan's bits
  u32 bits = 0, bitCount = 0;
  bool stopped = false;  //at a marker: no more bits

  //Whether the data goes as far as count bytes, read from memory only as far as it's used: a game may give the size
  //of a whole buffer of pictures.
  auto has(u64 count) -> bool {
    if(count <= bytes.size()) return true;
    if(count > reached) return false;
    u32 old = bytes.size(), size = std::min<u64>(reached, std::max<u64>(count, u64(old) * 2 + 64_KiB));
    bytes.resize(size);
    memory->copyOut(bytes.data() + old, address + old, size - old);
    data = bytes.data();
    return true;
  }

  //The markers up to the scan (SOS), checked: 0, or the library's error. Only a 4:2:0 YCbCr picture is taken.
  auto header() -> u32 {
    if(!has(2) || data[0] != 0xff || data[1] != 0xd8) return JpegNotJpeg;
    at = 2;
    while(true) {
      while(has(at + 2) && data[at] == 0xff && data[at + 1] == 0xff) at++;  //fill bytes
      if(!has(at + 2)) return JpegBadMarkerLength;
      if(data[at] != 0xff) return JpegUnknownMarker;
      u32 marker = data[at + 1];
      at += 2;
      if(marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;  //TEM, RSTn: no length
      if(marker == 0xd8 || marker == 0xd9) return marker == 0xd9 ? JpegNotJpeg : JpegUnknownMarker;
      if(!has(at + 2)) return JpegBadMarkerLength;
      u32 length = data[at] << 8 | data[at + 1];
      if(length < 2 || !has(u64(at) + length)) return JpegBadMarkerLength;
      u32 end = at + length;
      at += 2;
      if(u32 error = segment(marker, end)) return error;
      at = end;
      if(marker == 0xda) return 0;  //the scan's data follows
    }
  }

  auto segment(u32 marker, u32 end) -> u32 {
    auto left = [&](u32 count) { return at + count <= end; };
    if((marker >= 0xe0 && marker <= 0xef) || marker == 0xfe) return 0;  //APPn, COM
    if(marker == 0xdb) {  //DQT
      while(at < end) {
        u32 precision = data[at] >> 4, index = data[at] & 15, length = precision ? 128 : 64;
        if(index > 3 || precision > 1 || !left(1 + length)) return JpegBadMarkerLength;
        at++;
        for(u32 k = 0; k < 64; k++) {
          quantizers[index][k] = precision ? data[at + k * 2] << 8 | data[at + k * 2 + 1] : data[at + k];
        }
        at += length;
        quantizerDefined[index] = true;
      }
      return 0;
    }
    if(marker == 0xc4) {  //DHT
      while(at < end) {
        if(!left(17)) return JpegBadMarkerLength;
        u32 kind = data[at] >> 4, index = data[at] & 15, total = 0;
        for(u32 n = 0; n < 16; n++) total += data[at + 1 + n];
        if(kind > 1 || index > 3 || total > 256 || !left(17 + total)) return JpegBadMarkerLength;
        auto& table = kind ? acTables[index] : dcTables[index];
        if(!table.build(data + at + 1, data + at + 17)) return JpegBadMarkerLength;
        at += 17 + total;
      }
      return 0;
    }
    if(marker == 0xdd) {  //DRI
      if(!left(2)) return JpegBadMarkerLength;
      restartInterval = data[at] << 8 | data[at + 1];
      return 0;
    }
    if(marker == 0xc0 || marker == 0xc1) {  //SOF0 and SOF1: baseline and extended sequential, Huffman coded
      if(!left(6)) return JpegBadMarkerLength;
      u32 precision = data[at];
      height = data[at + 1] << 8 | data[at + 2];
      width = data[at + 3] << 8 | data[at + 4];
      componentCount = data[at + 5];
      at += 6;
      if(!left(componentCount * 3)) return JpegBadMarkerLength;
      if(precision != 8) return JpegUnsupportedColorspace;
      if(componentCount != 3 || !width || !height) return JpegUnsupportedSampling;
      for(u32 n = 0; n < 3; n++) {
        auto& component = components[n];
        component.id = data[at + n * 3];
        component.h = data[at + n * 3 + 1] >> 4;
        component.v = data[at + n * 3 + 1] & 15;
        component.quantizer = data[at + n * 3 + 2] & 3;
      }
      if(components[0].h != 2 || components[0].v != 2) return JpegUnsupportedSampling;
      for(u32 n : {1, 2}) if(components[n].h != 1 || components[n].v != 1) return JpegUnsupportedSampling;
      frame = true;
      return 0;
    }
    if(marker == 0xda) {  //SOS: all three components, interleaved, as a 4:2:0 picture's single scan is
      if(!frame || !left(1)) return JpegNotJpeg;
      u32 count = data[at++];
      if(count != 3 || !left(count * 2 + 3)) return JpegUnsupportedSampling;
      for(u32 n = 0; n < count; n++) {
        u32 id = data[at + n * 2], tables = data[at + n * 2 + 1];
        if(components[n].id != id) return JpegUnsupportedSampling;
        components[n].dc = tables >> 4 & 3;
        components[n].ac = tables & 3;
      }
      for(auto& component : components) {
        if(!quantizerDefined[component.quantizer]) return JpegNotJpeg;
      }
      //tables the picture doesn't give: Annex K's, as Motion JPEG takes them
      if(!dcTables[0].defined) dcTables[0].build(jpegLumaDCCounts, jpegDCValues);
      if(!dcTables[1].defined) dcTables[1].build(jpegChromaDCCounts, jpegDCValues);
      if(!acTables[0].defined) acTables[0].build(jpegLumaACCounts, jpegLumaACValues);
      if(!acTables[1].defined) acTables[1].build(jpegChromaACCounts, jpegChromaACValues);
      for(auto& component : components) {
        if(!dcTables[component.dc].defined || !acTables[component.ac].defined) return JpegNotJpeg;
      }
      return 0;
    }
    return JpegUnknownMarker;
  }

  //The scan's next bit: bytes 0xff 0x00 stand for 0xff; a marker, or the data's end, gives zeros.
  auto bit() -> u32 {
    if(!bitCount) {
      u32 byte = 0;
      if(!stopped && has(at + 1)) {
        byte = data[at];
        if(byte != 0xff) at++;
        else if(has(at + 2) && data[at + 1] == 0x00) at += 2;
        else byte = 0, stopped = true;
      }
      bits = byte, bitCount = 8;
    }
    return bits >> --bitCount & 1;
  }

  auto receive(u32 count) -> s32 {
    s32 value = 0;
    for(u32 n = 0; n < count; n++) value = value << 1 | bit();
    return value;
  }

  //F.12's EXTEND: count bits as a signed difference
  auto extend(u32 count) -> s32 {
    if(!count) return 0;
    s32 value = receive(count);
    return value < 1 << (count - 1) ? value - (1 << count) + 1 : value;
  }

  auto huffman(const JpegTable& table) -> u32 {
    s32 code = 0;
    for(u32 length = 1; length <= 16; length++) {
      code = code << 1 | bit();
      if(code <= table.maxCode[length]) return table.values[table.first[length] + code - table.minCode[length]];
    }
    return 0;
  }

  //After a restart interval: its marker (RSTn) passed over, the bits and predictions started afresh.
  auto restart() -> void {
    bitCount = 0;
    while(has(at + 2) && data[at] == 0xff && data[at + 1] == 0xff) at++;
    if(has(at + 2) && data[at] == 0xff && data[at + 1] >= 0xd0 && data[at + 1] <= 0xd7) at += 2;
    stopped = false;
    for(auto& component : components) component.prediction = 0;
  }

  //The scan, decoded MCU by MCU (a 4:2:0 picture's are 16x16: four Y blocks, a Cb and a Cr) into the planes.
  auto decode() -> void {
    u32 across = (width + 15) / 16, down = (height + 15) / 16;
    for(auto& component : components) {
      component.stride = across * component.h * 8;
      component.plane.assign(u64(component.stride) * down * component.v * 8, 0);
      component.prediction = 0;
    }
    u32 left = restartInterval;
    for(u32 mcuY = 0; mcuY < down; mcuY++) {
      for(u32 mcuX = 0; mcuX < across; mcuX++) {
        if(restartInterval && !left) restart(), left = restartInterval;
        for(auto& component : components) {
          for(u32 v = 0; v < component.v; v++) {
            for(u32 h = 0; h < component.h; h++) {
              u32 x = (mcuX * component.h + h) * 8, y = (mcuY * component.v + v) * 8;
              block(component, &component.plane[u64(y) * component.stride + x]);
            }
          }
        }
        left--;
      }
    }
  }

  auto block(Component& component, u8* out) -> void {
    static const auto zigzag = [] {
      std::array<u8, 64> order{};
      u32 k = 0;
      for(u32 diagonal = 0; diagonal < 15; diagonal++) {
        for(u32 i = 0; i <= diagonal; i++) {
          u32 row = diagonal & 1 ? i : diagonal - i, column = diagonal - row;
          if(row < 8 && column < 8) order[k++] = row * 8 + column;
        }
      }
      return order;
    }();
    const u16* q = quantizers[component.quantizer];
    s32 coefficients[64] = {};
    //(held to 16 bits, as no 8-bit picture's DC terms leave them: a damaged one's differences can't add up past them)
    s32 difference = extend(huffman(dcTables[component.dc]) & 15);
    component.prediction = std::clamp(component.prediction + difference, -32768, 32767);
    coefficients[0] = component.prediction * q[0];
    for(u32 k = 1; k < 64;) {
      u32 symbol = huffman(acTables[component.ac]), run = symbol >> 4, magnitude = symbol & 15;
      if(!magnitude) {
        if(run != 15) break;
        k += 16;
        continue;
      }
      k += run;
      if(k > 63) break;
      coefficients[zigzag[k]] = extend(magnitude) * q[k];
      k++;
    }
    idct(coefficients, out, component.stride);
  }

  //A.3.3's inverse DCT, a row pass then a column pass, level shifted by 128 and rounded to the nearest.
  static auto idct(const s32 in[64], u8* out, u32 stride) -> void {
    static const auto basis = [] {
      std::array<double, 64> table{};
      for(u32 u = 0; u < 8; u++) {
        for(u32 x = 0; x < 8; x++) {
          table[u * 8 + x] = (u ? 1.0 : std::sqrt(0.5)) / 2 * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16);
        }
      }
      return table;
    }();
    double rows[64];
    for(u32 y = 0; y < 8; y++) {
      for(u32 x = 0; x < 8; x++) {
        double sum = 0;
        for(u32 u = 0; u < 8; u++) sum += basis[u * 8 + x] * in[y * 8 + u];
        rows[y * 8 + x] = sum;
      }
    }
    for(u32 x = 0; x < 8; x++) {
      for(u32 y = 0; y < 8; y++) {
        double sum = 0;
        for(u32 v = 0; v < 8; v++) sum += basis[v * 8 + y] * rows[v * 8 + x];
        out[y * stride + x] = u8(std::lround(std::clamp(sum + 128, 0.0, 255.0)));
      }
    }
  }

  //The bytes the planes take as the library gives them.
  auto planarSize() const -> u64 {
    return u64(width) * height + 2 * u64((width + 1) / 2) * ((height + 1) / 2);
  }

  //The planes' rows in the order the library gives them: Y, then Cb and Cr, each as wide as it is (the colours half
  //the picture's width and height, rounded up).
  template<typename F> auto planarRows(const F& row) const -> void {
    u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
    for(u32 y = 0; y < height; y++) row(&components[0].plane[u64(y) * components[0].stride], width);
    for(u32 n : {1, 2}) {
      for(u32 y = 0; y < halfHeight; y++) row(&components[n].plane[u64(y) * components[n].stride], half);
    }
  }
};

//The YCbCr picture's bytes a conversion reads: as far as memory goes, zeros beyond (mjpegcsc's null source reads
//as zeros: its pixel, 0x00008600, is Y, Cb and Cr 0's).
auto jpegReadBytes(Memory& memory, u32 address, u64 size) -> std::vector<u8> {
  std::vector<u8> bytes(size, 0);
  if(size <= 0xffff'ffff && memory.copyOut(bytes.data(), address, size)) return bytes;
  for(u64 n = 0; n < size; n++) {
    if(u8* byte = memory.pointer(address + u32(n))) bytes[n] = *byte;
  }
  return bytes;
}

//One pixel from Y, Cb and Cr: sceJpegCsc's sums (precise) or sceJpegMJpegCsc's (8-bit coefficients, Cb and Cr from
//1), 0x00BBGGRR.
auto jpegPixel(u32 y, u32 cb, u32 cr, bool engine) -> u32 {
  s32 d = s32(cb) - 128, e = s32(cr) - 128, r, g, b;
  if(engine) {
    d = std::max(d, -127), e = std::max(e, -127);
    r = s32(y) + ((359 * e + 128) >> 8);
    g = s32(y) + ((-88 * d - 182 * e + 128) >> 8);
    b = s32(y) + ((454 * d + 128) >> 8);
  } else {
    r = s32(y) + ((91881 * e + 32768) >> 16);
    g = s32(y) + ((-22554 * d - 46802 * e + 32768) >> 16);
    b = s32(y) + ((116130 * d + 32768) >> 16);
  }
  return std::clamp(r, 0, 255) | std::clamp(g, 0, 255) << 8 | std::clamp(b, 0, 255) << 16;
}

//Whether a buffer reaches the kernel's half of the address space, as the library checks each it's given (its
//address, its end or its size with the top bit set): PRIV_REQUIRED (decode's huge and negative sizes).
auto jpegKernelAddress(u32 address, u32 size) -> bool {
  return (address | (address + size) | size) & 0x8000'0000;
}

//How much of a buffer memory reaches from its start, up to size.
auto jpegReach(Memory& memory, u32 address, u32 size) -> u32 {
  if(memory.reaches(address, size)) return size;
  u32 low = 0, high = size;  //low reached, high not
  while(high - low > 1) {
    u32 middle = low + (high - low) / 2;
    if(memory.reaches(address, middle)) low = middle; else high = middle;
  }
  return low;
}

//A picture's data read and its markers checked, as each decoding function does once its buffers' addresses have
//passed: no data (nullData: what each function says to it), none at all (BAD_MARKER_LENGTH, decode's "Size 0"),
//then the picture's own markers. A picture whose planes would take more than the PSP's memory is refused as too
//large (chosen: no PSP could hold it). 0, or the error.
auto jpegParse(Memory& memory, u32 address, u32 size, u32 nullData, JpegPicture& picture) -> u32 {
  if(!address) return nullData;
  if(!size) return JpegBadMarkerLength;
  picture.memory = &memory;
  picture.address = address;
  picture.reached = jpegReach(memory, address, size);
  if(u32 error = picture.header()) return error;
  if(picture.planarSize() > 64_MiB) return JpegUnsupportedImageSize;
  return 0;
}

//A row of pixels written at address, as far as memory reaches.
auto jpegWriteRow(Memory& memory, u32 address, const std::vector<u8>& row) -> void {
  if(memory.copyIn(address, row.data(), row.size())) return;
  for(u32 n = 0; n + 4 <= row.size(); n += 4) {
    if(memory.reaches(address + n, 4)) memory.copyIn(address + n, &row[n], 4);
  }
}

//The Media Engine's transfers (sceJpegMJpegCsc's) go 8 bytes at a time, from and to an address rounded down to 8,
//and a byte's place in its 8 is its own address's: a run starting 4 bytes past a multiple of 8 has the second half
//of each 8 bytes it reads come from 8 bytes before, and written there too. That is what mjpegcsc's patterns recorded
//through its buffer 4 bytes past such an address (the first 16 pixels' Y, Cb and Cr from before each plane), and its
//rows a pixel apart writing every byte but their last four; aligned runs are read and written as they are.
auto jpegBurst(u32 address, u32 offset) -> u32 {
  return (address & ~7u) + (offset & ~7u) + ((address + offset) & 7);
}

auto jpegReadBurst(Memory& memory, u32 address, u32 size) -> std::vector<u8> {
  if(!(address & 7)) return jpegReadBytes(memory, address, size);
  auto raw = jpegReadBytes(memory, address & ~7u, u64(size) + 8);
  std::vector<u8> bytes(size);
  for(u32 n = 0; n < size; n++) bytes[n] = raw[jpegBurst(address, n) - (address & ~7u)];
  return bytes;
}

auto jpegWriteBurst(Memory& memory, u32 address, const std::vector<u8>& row) -> void {
  if(!(address & 7)) return jpegWriteRow(memory, address, row);
  u32 base = address & ~7u, span = (u32(row.size()) + 7) & ~7u;  //the 8-byte groups the row is written to
  std::vector<u8> bytes(span);
  if(!memory.copyOut(bytes.data(), base, span)) {
    for(u32 n = 0; n < row.size(); n++) {
      u32 at = jpegBurst(address, n);
      if(memory.reaches(at, 1)) memory.copyIn(at, &row[n], 1);
    }
    return;
  }
  for(u32 n = 0; n < row.size(); n++) bytes[jpegBurst(address, n) - base] = row[n];
  memory.copyIn(base, bytes.data(), span);
}

constexpr u32 JpegMicroseconds = 300;  //how long a call on the Media Engine waits (chosen, as atrac.cpp's)
constexpr u32 JpegNotInitialized = 0x8000'0001;  //uOFW's SCE_ERROR_NOT_INITIALIZED: decoding before the library starts

}

//The library's calls that use the Media Engine wait for it, other threads running meanwhile, as pspautotests' jpeg
//tests' checkpoints show: they report a thread switch during sceJpegInitMJpeg and sceJpegFinishMJpeg when they
//succeed, during the decoding functions and sceJpegGetOutputInfo once their buffers' addresses have passed
//(whatever the picture), and during the conversions when they convert; never during the context's calls. (Twice,
//init and finish didn't switch where an earlier call had kept the Media Engine busy long enough: csc's init,
//mjpegcsc's finish.)

//(): the library started for the decoder: refused if it's started already.
auto Kernel::sceJpegInitMJpeg() -> void {
  if(jpeg.initialized) return result(JpegAlreadyInitialized);
  jpeg.initialized = true;
  result(0);
  codecWait(JpegMicroseconds);
}

//(): the library ended: refused if it isn't started, or while its context is made (finish's "Finish in use").
auto Kernel::sceJpegFinishMJpeg() -> void {
  if(!jpeg.initialized || jpeg.created) return result(JpegWrongState);
  jpeg.initialized = false;
  result(0);
  codecWait(JpegMicroseconds);
}

//(width, height): the context made, for a picture buffer of that size: refused unless the library is started and no
//context is made, or wider than 1024 pixels (compared signed: create's -1 and 0x80000000 are taken). Any height.
auto Kernel::sceJpegCreateMJpeg() -> void {
  s32 width = s32(arg(0)), height = s32(arg(1));
  if(!jpeg.initialized || jpeg.created) return result(JpegWrongState);
  if(width > 1024) return result(JpegUnsupportedImageSize);
  jpeg.created = true;
  jpeg.width = width, jpeg.height = height;
  result(0);
}

//(): the context taken away (its size stays: decode's "After delete"), refused if there's none.
auto Kernel::sceJpegDeleteMJpeg() -> void {
  if(!jpeg.initialized || !jpeg.created) return result(JpegWrongState);
  jpeg.created = false;
  result(0);
}

//(data, size, where to put the colour information, mode): the bytes a YCbCr buffer needs for the picture, and its
//colour information as the conversions take it (0x00020202: two colour planes, at half the width and half the
//height), written if there's somewhere to. No need of the library or its context; the mode changes nothing.
auto Kernel::sceJpegGetOutputInfo() -> void {
  if(jpegKernelAddress(arg(0), arg(1))) return result(ErrorPrivilegeRequired);
  JpegPicture picture;
  if(u32 error = jpegParse(memory, arg(0), arg(1), JpegNotJpeg, picture)) {
    result(error);
  } else {
    if(arg(2) && memory.reaches(arg(2), 4)) memory.write(4, arg(2), 0x0002'0202);
    result(picture.planarSize());
  }
  codecWait(JpegMicroseconds);
}

//(data, size, pixels, mode): the picture decoded into 32-bit pixels, a row of it to a row of the context's width.
//After the data's checks: no pixels (INVALID_POINTER, uOFW's), a picture wider or taller than the context
//(UNSUPPORT_IMAGE_SIZE: before the library starts too, its context then 0 by 0), then the library not started
//(NOT_INITIALIZED, decode's "After finish"). sceJpegDecodeMJpegSuccessively's recordings are the same.
auto Kernel::sceJpegDecodeMJpeg() -> void {
  if(jpegKernelAddress(arg(0), arg(1))) return result(ErrorPrivilegeRequired);
  result(jpegDecodePixels(arg(0), arg(1), arg(2)));
  codecWait(JpegMicroseconds);
}

auto Kernel::jpegDecodePixels(u32 data, u32 size, u32 pixels) -> u32 {
  JpegPicture picture;
  if(u32 error = jpegParse(memory, data, size, JpegNotJpeg, picture)) return error;
  if(!pixels) return ErrorInvalidPointer;
  if(s32(picture.width) > jpeg.width || s32(picture.height) > jpeg.height) return JpegUnsupportedImageSize;
  if(!jpeg.initialized) return JpegNotInitialized;
  picture.decode();
  auto& luma = picture.components[0];
  auto& blue = picture.components[1];
  auto& red = picture.components[2];
  std::vector<u8> row(picture.width * 4);
  for(u32 y = 0; y < picture.height; y++) {
    for(u32 x = 0; x < picture.width; x++) {
      u32 c = (y / 2) * blue.stride + x / 2;
      u32 pixel = jpegPixel(luma.plane[y * luma.stride + x], blue.plane[c], red.plane[c], true);
      for(u32 n = 0; n < 4; n++) row[x * 4 + n] = pixel >> n * 8;
    }
    jpegWriteRow(memory, pixels + y * u32(jpeg.width) * 4, row);
  }
  return picture.width << 16 | picture.height;
}

//(data, size, YCbCr buffer, its size, mode): the picture decoded into its planes, Y then Cb then Cr. No need of the
//library or its context. Checked before it waits: the data reaching the kernel's addresses (PRIV_REQUIRED), no data
//(0x80650051), the buffer reaching them (PRIV_REQUIRED); after: the picture, then a buffer smaller than its planes
//(0x80650041). A null buffer with room enough is taken, and gets nothing (decodeycbcr's "Output NULL"; the PSP wrote
//somewhere).
auto Kernel::sceJpegDecodeMJpegYCbCr() -> void {
  u32 data = arg(0), size = arg(1), buffer = arg(2), room = arg(3);
  if(jpegKernelAddress(data, size)) return result(ErrorPrivilegeRequired);
  if(!data) return result(JpegNullBuffer);
  if(jpegKernelAddress(buffer, room)) return result(ErrorPrivilegeRequired);
  result(jpegDecodeYCbCr(data, size, buffer, room, false));
  codecWait(JpegMicroseconds);
}

//sceJpegDecodeMJpegYCbCr's sibling, as decodeycbcrs recorded it: no data is found after the wait (0x80650023, as
//data that isn't a JPEG), and a null buffer is INVALID_POINTER (pspjpeg.h's) once its size has passed.
auto Kernel::sceJpegDecodeMJpegYCbCrSuccessively() -> void {
  u32 data = arg(0), size = arg(1), buffer = arg(2), room = arg(3);
  if(jpegKernelAddress(data, size) || jpegKernelAddress(buffer, room)) return result(ErrorPrivilegeRequired);
  result(jpegDecodeYCbCr(data, size, buffer, room, true));
  codecWait(JpegMicroseconds);
}

auto Kernel::jpegDecodeYCbCr(u32 data, u32 size, u32 buffer, u32 room, bool successively) -> u32 {
  JpegPicture picture;
  if(u32 error = jpegParse(memory, data, size, JpegNotJpeg, picture)) return error;
  if(room < picture.planarSize()) return JpegBufferTooSmall;
  if(!buffer && successively) return JpegInvalidPointer;
  picture.decode();
  u32 reached = buffer ? jpegReach(memory, buffer, picture.planarSize()) : 0, offset = 0;
  picture.planarRows([&](const u8* row, u32 count) {
    if(offset < reached) memory.copyIn(buffer + offset, row, std::min(count, reached - offset));
    offset += count;
  });
  return picture.width << 16 | picture.height;
}

//(destination, YCbCr planes, width << 16 | height, buffer width, colour information): the planes converted into
//32-bit pixels, a row of the picture to a row of the buffer's width (in pixels, as many bytes apart as four times it
//makes, wrapping round: csc's stride of 0x80000000 writes every row in the same place). The colour information is
//sceJpegGetOutputInfo's: two colour planes (its bits 16-19), at half the width or the whole (bits 8-15: 2 or 1) and
//half the height or the whole (bits 0-7), 2 by 2, 2 by 1 or 1 by 1; anything else is UNSUPPORT_COLORSPACE. Each
//plane is as wide as the picture's width, the colours' as their half of it, rounded up. No need of the library.
//No buffer: 0x80650051. A height of 0 converts one row and returns -1, as csc recorded.
auto Kernel::sceJpegCsc() -> void {
  u32 destination = arg(0), source = arg(1), size = arg(2), stride = arg(3), colour = arg(4);
  if(!destination || !source) return result(JpegNullBuffer);
  u32 across = colour >> 8 & 0xff, down = colour & 0xff;
  if((colour >> 16 & 15) != 2 || !((across == 2 && down == 2) || (across == 2 && down == 1) ||
                                    (across == 1 && down == 1))) {
    return result(JpegUnsupportedColorspace);
  }
  jpegConvert(destination, source, size, stride, across, down, false);
  result(size & 0xffff ? 0 : -1);
  codecWait(JpegMicroseconds);
}

//(destination, YCbCr planes, width << 16 | height, buffer width): sceJpegCsc for 4:2:0 planes on the Media Engine,
//with its own sums (above) and its transfers (jpegBurst()). The library must be started (NOT_INITIALIZED), the
//destination given (INVALID_POINTER, uOFW's), and the picture 720 by 480 at most with a buffer width of 1024 at most
//(UNSUPPORT_IMAGE_SIZE; compared signed, so a negative one is taken). The Media Engine keeps the buffer width in 11
//bits: a negative one is taken modulo 2048 (mjpegcsc's -4 wrote its first eight rows 8176 bytes apart, as 2044's
//would be; 0x80000000 all in one place). A null source reads as zeros. A height of 0 converts one row. It waits for
//the Media Engine only for 16 rows or more: mjpegcsc's pictures less high reported no thread switch.
auto Kernel::sceJpegMJpegCsc() -> void {
  u32 destination = arg(0), source = arg(1), size = arg(2), stride = arg(3);
  if(!jpeg.initialized) return result(JpegNotInitialized);
  if(!destination) return result(ErrorInvalidPointer);
  if(size >> 16 > 720 || (size & 0xffff) > 480 || s32(stride) > 1024) return result(JpegUnsupportedImageSize);
  jpegConvert(destination, source, size, stride & 0x7ff, 2, 2, true);
  result(0);
  if((size & 0xffff) >= 16) codecWait(JpegMicroseconds);
}

//The conversion both do: width by height pixels (one row when the height is 0) from Y, Cb and Cr planes one after
//another at source, each colour sample standing for across by down pixels; the Media Engine's (engine) with its own
//sums and transfers. As many rows are converted as fit in 64 MiB of pixels (chosen: no PSP has room for more).
auto Kernel::jpegConvert(u32 destination, u32 source, u32 size, u32 stride, u32 across, u32 down, bool engine)
  -> void {
  u32 width = size >> 16, height = size & 0xffff, rows = height ? height : 1;
  if(!width) return;
  rows = std::min<u64>(rows, 16_MiB / width);
  u32 colourWidth = (width + across - 1) / across, colourHeight = (height + down - 1) / down;
  u32 blue = source + width * height, red = blue + colourWidth * colourHeight;
  auto read = [&](u32 address, u32 count) {
    return engine ? jpegReadBurst(memory, address, count) : jpegReadBytes(memory, address, count);
  };
  std::vector<u8> row(width * 4);
  for(u32 y = 0; y < rows; y++) {
    auto luma = read(source + y * width, width);
    auto cb = read(blue + y / down * colourWidth, colourWidth);
    auto cr = read(red + y / down * colourWidth, colourWidth);
    for(u32 x = 0; x < width; x++) {
      u32 pixel = jpegPixel(luma[x], cb[x / across], cr[x / across], engine);
      for(u32 n = 0; n < 4; n++) row[x * 4 + n] = pixel >> n * 8;
    }
    if(engine) jpegWriteBurst(memory, destination + y * stride * 4, row);
    else jpegWriteRow(memory, destination + y * stride * 4, row);
  }
}
