//JPEG pictures made up for the tests (jpeg.cpp): Y, Cb and Cr planes, the colours at half the picture's size each way
//(4:2:0, what sceJpeg decodes), coded as ITU-T T.81 lays a baseline picture out: a forward DCT (A.3.3) quantized by
//one table for every component, the blocks in MCUs of four Y blocks, a Cb and a Cr, and Annex K's Huffman tables,
//written in a DHT segment or left out (as Motion JPEG's pictures leave them), with restart markers when asked for.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace jpeg_maker {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
constexpr double Pi = 3.14159265358979323846;

//Annex K's tables (K.3 to K.6), as a picture's DHT segment gives them: how many codes of each length, then the values
inline const u8 LumaDC[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
inline const u8 ChromaDC[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
inline const u8 DCValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline const u8 LumaAC[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
inline const u8 LumaACValues[162] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
  0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
  0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
  0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65,
  0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
  0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
  0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
  0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
inline const u8 ChromaAC[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
inline const u8 ChromaACValues[162] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
  0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
  0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
  0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86,
  0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
  0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

//A Huffman table's codes by value (C.2's procedure: codes counted up, a bit longer for each length)
struct Codes {
  u16 code[256] = {};
  u8 length[256] = {};
  Codes(const u8 counts[16], const u8* values) {
    u32 code = 0, k = 0;
    for(u32 bits = 1; bits <= 16; bits++, code <<= 1) {
      for(u32 n = 0; n < counts[bits - 1]; n++, k++, code++) this->code[values[k]] = code, length[values[k]] = bits;
    }
  }
};

struct Picture {
  u32 width = 16, height = 16;
  std::vector<u8> y, cb, cr;  //y width by height, the colours half that each way (rounded up), row by row
  u32 quantizer = 1;          //every entry of the one table
  u32 restart = 0;            //MCUs between restart markers (0: none)
  bool tables = true;         //write Annex K's tables in a DHT segment (else leave them out)

  auto sample(const std::vector<u8>& plane, u32 planeWidth, u32 planeHeight, u32 x, u32 y) const -> s32 {
    return plane[std::min(y, planeHeight - 1) * planeWidth + std::min(x, planeWidth - 1)];  //edges repeated
  }

  auto make() const -> std::vector<u8> {
    std::vector<u8> out = {0xff, 0xd8};
    auto segment = [&](u8 marker, const std::vector<u8>& body) {
      out.insert(out.end(), {0xff, marker, u8((body.size() + 2) >> 8), u8(body.size() + 2)});
      out.insert(out.end(), body.begin(), body.end());
    };
    std::vector<u8> dqt = {0x00};
    dqt.resize(65, quantizer);
    segment(0xdb, dqt);
    segment(0xc0, {8, u8(height >> 8), u8(height), u8(width >> 8), u8(width), 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
    if(tables) {
      std::vector<u8> dht;
      auto table = [&](u8 kind, const u8* counts, const u8* values, u32 total) {
        dht.push_back(kind);
        dht.insert(dht.end(), counts, counts + 16);
        dht.insert(dht.end(), values, values + total);
      };
      table(0x00, LumaDC, DCValues, 12), table(0x10, LumaAC, LumaACValues, 162);
      table(0x01, ChromaDC, DCValues, 12), table(0x11, ChromaAC, ChromaACValues, 162);
      segment(0xc4, dht);
    }
    if(restart) segment(0xdd, {u8(restart >> 8), u8(restart)});
    segment(0xda, {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0});

    //the scan: bits gathered, a byte at a time with 0xff stuffed
    u32 bits = 0, count = 0;
    auto put = [&](u32 value, u32 length) {
      for(u32 n = length; n--;) {
        bits = bits << 1 | (value >> n & 1);
        if(++count == 8) {
          out.push_back(bits);
          if(bits == 0xff) out.push_back(0);
          bits = count = 0;
        }
      }
    };
    auto flush = [&] { while(count) put(1, 1); };
    Codes lumaDC{LumaDC, DCValues}, lumaAC{LumaAC, LumaACValues};
    Codes chromaDC{ChromaDC, DCValues}, chromaAC{ChromaAC, ChromaACValues};
    std::vector<u32> zigzag;
    for(u32 diagonal = 0; diagonal < 15; diagonal++) {
      for(u32 i = 0; i <= diagonal; i++) {
        u32 row = diagonal & 1 ? i : diagonal - i, column = diagonal - row;
        if(row < 8 && column < 8) zigzag.push_back(row * 8 + column);
      }
    }
    auto magnitude = [](s32 value) { u32 size = 0; for(u32 a = std::abs(value); a; a >>= 1) size++; return size; };
    s32 predictions[3] = {};
    auto block = [&](u32 component, const std::vector<u8>& plane, u32 planeWidth, u32 planeHeight, u32 bx, u32 by) {
      double coefficients[64];
      for(u32 v = 0; v < 8; v++) {
        for(u32 u = 0; u < 8; u++) {
          double sum = 0;
          for(u32 y = 0; y < 8; y++) {
            for(u32 x = 0; x < 8; x++) {
              sum += (sample(plane, planeWidth, planeHeight, bx * 8 + x, by * 8 + y) - 128)
                   * std::cos((2 * x + 1) * u * Pi / 16) * std::cos((2 * y + 1) * v * Pi / 16);
            }
          }
          coefficients[v * 8 + u] = sum / 4 * (u ? 1 : std::sqrt(0.5)) * (v ? 1 : std::sqrt(0.5));
        }
      }
      s32 quantized[64];
      for(u32 k = 0; k < 64; k++) quantized[k] = s32(std::lround(coefficients[zigzag[k]] / quantizer));
      auto& dc = component ? chromaDC : lumaDC;
      auto& ac = component ? chromaAC : lumaAC;
      s32 difference = quantized[0] - predictions[component];
      predictions[component] = quantized[0];
      u32 size = magnitude(difference);
      put(dc.code[size], dc.length[size]);
      put(difference < 0 ? difference + (1 << size) - 1 : difference, size);
      u32 run = 0;
      for(u32 k = 1; k < 64; k++) {
        if(!quantized[k]) { run++; continue; }
        for(; run >= 16; run -= 16) put(ac.code[0xf0], ac.length[0xf0]);
        u32 bitsize = magnitude(quantized[k]);
        put(ac.code[run << 4 | bitsize], ac.length[run << 4 | bitsize]);
        put(quantized[k] < 0 ? quantized[k] + (1 << bitsize) - 1 : quantized[k], bitsize);
        run = 0;
      }
      if(run) put(ac.code[0x00], ac.length[0x00]);
    };
    u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
    u32 across = (width + 15) / 16, down = (height + 15) / 16, made = 0, marker = 0;
    for(u32 my = 0; my < down; my++) {
      for(u32 mx = 0; mx < across; mx++) {
        if(restart && made && made % restart == 0) {
          flush();
          out.insert(out.end(), {0xff, u8(0xd0 + marker++ % 8)});
          for(auto& prediction : predictions) prediction = 0;
        }
        for(u32 n = 0; n < 4; n++) block(0, y, width, height, mx * 2 + n % 2, my * 2 + n / 2);
        block(1, cb, half, halfHeight, mx, my);
        block(2, cr, half, halfHeight, mx, my);
        made++;
      }
    }
    flush();
    out.insert(out.end(), {0xff, 0xd9});
    return out;
  }
};

}
