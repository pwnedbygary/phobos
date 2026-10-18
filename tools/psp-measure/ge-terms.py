#!/usr/bin/env python3
"""The GE timing model's terms, counted per run of the GE, for ge-fit.py: patches a copy of Phobos's source (never the
tree you work in) so that a runner built from it, run with the GE timing "PSP" and PHOBOS_GE_TERMS=FILE, writes each run
of the GE (Kernel::geTime()) as a line "R term=count ...": for each of GE::Cost's terms, the count its clocks multiply
(words, vertices' bytes, pixels, texels' bytes by their rate, transfers' rows and bytes...), the front end's apart from
the back end's ("F."/"B." before the term). Besides: "S" as a list starts (GE::Cost::List), "Q address stall
callbacks" as one is queued, "U" as a stall address is moved, and "O path" as the program creates a file (psp-measure
writes each test's results so: what ends one test and starts the next).

usage: ge-terms.py SRC   (SRC: e.g. git archive HEAD ares nall libco thirdparty tools/psp-runner tests/psp/ares | tar -x)
Then build tools/psp-runner from SRC, run psp-measure's rounds (README.md, "The GE's time") and give ge-fit.py the
files."""
import os
import sys

src = sys.argv[1]


def edit(path, old, new, count=1):
    full = os.path.join(src, path)
    text = open(full, encoding="utf-8").read()
    if text.count(old) != count:
        sys.exit("ge-terms.py: %s: %d places for %r (the source has moved on from this tool)" % (path, text.count(old),
                                                                                                old[:70]))
    open(full, "w", encoding="utf-8").write(text.replace(old, new))


edit("ares/psp/ge/ge.hpp", "#include <vector>\n", r'''#include <vector>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace geterms {
inline std::map<std::string, double>& counts() { static std::map<std::string, double> m; return m; }
inline FILE* out() {
  static FILE* file = [] { const char* p = std::getenv("PHOBOS_GE_TERMS"); return p ? std::fopen(p, "w") : nullptr; }();
  return file;
}
inline void add(const char* side, const std::string& term, double count) {
  if(count != 0 && out()) counts()[std::string(side) + "." + term] += count;
}
inline void line(const char* tag, const std::string& text) { if(auto o = out()) std::fprintf(o, "%s %s\n", tag, text.c_str()); }
inline void run() {
  if(auto o = out()) {
    std::fprintf(o, "R");
    for(auto& [k, v] : counts()) std::fprintf(o, " %s=%.10g", k.c_str(), v);
    std::fprintf(o, "\n");
  }
  counts().clear();
}
inline std::string indexed(const char* name, std::initializer_list<unsigned> at) {
  std::string s = name;
  for(auto n : at) s += "[" + std::to_string(n) + "]";
  return s;
}
}
''')

#the back end: a primitive's setup, rows or pixels, the depth test, its texels
edit("ares/psp/ge/draw.cpp", '''  f64 back = Cost::Primitive;
  if(pixels && rows > 0) {
    f64 rate = columns <= Cost::NarrowColumns ? Cost::NarrowPixel : p.format == 3 ? Cost::Pixel32 : Cost::Pixel16;
    back += f64(rows) * std::max(Cost::Row, f64(pixels) / f64(rows) * rate);
    if(p.format == 3 && (p.clear ? p.clearDepth : p.depthTest)) back += f64(pixels) * Cost::DepthPixel;''', '''  f64 back = Cost::Primitive;
  geterms::add("B", "Primitive", 1);
  if(pixels && rows > 0) {
    f64 rate = columns <= Cost::NarrowColumns ? Cost::NarrowPixel : p.format == 3 ? Cost::Pixel32 : Cost::Pixel16;
    back += f64(rows) * std::max(Cost::Row, f64(pixels) / f64(rows) * rate);
    if(f64(pixels) / f64(rows) * rate >= Cost::Row) {
      geterms::add("B", columns <= Cost::NarrowColumns ? "NarrowPixel" : p.format == 3 ? "Pixel32" : "Pixel16", f64(pixels));
    } else {
      geterms::add("B", "Row", f64(rows));
    }
    if(p.format == 3 && (p.clear ? p.clearDepth : p.depthTest)) back += f64(pixels) * Cost::DepthPixel,
      geterms::add("B", "DepthPixel", f64(pixels));''')
edit("ares/psp/ge/draw.cpp", '''      clocks = (c.fetched ? 0 : Cost::FetchFixed) + fresh * Cost::FetchByte[(vram ? 2 : 0) + !t.swizzled];''',
     '''      clocks = (c.fetched ? 0 : Cost::FetchFixed) + fresh * Cost::FetchByte[(vram ? 2 : 0) + !t.swizzled];
      if(!c.fetched) geterms::add("B", "FetchFixed", 1);
      geterms::add("B", geterms::indexed("FetchByte", {(vram ? 2u : 0u) + !t.swizzled}), fresh);''')
edit("ares/psp/ge/draw.cpp", '''  if(t.format >= 8) return wide * Cost::CompressedByte;''',
     '''  if(t.format >= 8) return geterms::add("B", "CompressedByte", wide), wide * Cost::CompressedByte;''')
edit("ares/psp/ge/draw.cpp", '''  return (1 - rowsWide) * narrow * narrowRate + rowsWide * wide * Cost::TexelByte[indices][from][1];''',
     '''  if(sprite) geterms::add("B", geterms::indexed("TexelByte", {indices ? 1u : 0u, from, 0u}), (1 - rowsWide) * narrow);
  else geterms::add("B", geterms::indexed("TriangleTexelByte", {indices ? 1u : 0u, from}), (1 - rowsWide) * narrow);
  geterms::add("B", geterms::indexed("TexelByte", {indices ? 1u : 0u, from, 1u}), rowsWide * wide);
  return (1 - rowsWide) * narrow * narrowRate + rowsWide * wide * Cost::TexelByte[indices][from][1];''')
edit("ares/psp/ge/draw.cpp", '''    if(physical < Memory::VRAMBase || physical >= Memory::VRAMBase + Memory::VRAMWindow) back += share * Cost::RamShare;''',
     '''    if(physical < Memory::VRAMBase || physical >= Memory::VRAMBase + Memory::VRAMWindow) back += share * Cost::RamShare,
      geterms::add("B", "RamShare", share);''')
edit("ares/psp/ge/draw.cpp", '''  pipeItem(height * Cost::TransferRow + f64(width) * height * bytes * perByte, 1);''',
     '''  geterms::add("B", "TransferRow", height);
  geterms::add("B", geterms::indexed("TransferByte", {inVRAM(source) ? 1u : 0u, inVRAM(destination) ? 1u : 0u}),
               f64(width) * height * bytes);
  pipeItem(height * Cost::TransferRow + f64(width) * height * bytes * perByte, 1);''')
edit("ares/psp/ge/texture.cpp", '''  if(pspTiming) pipeDrain(), pipeItem(Cost::PaletteLoad + bytes * Cost::PaletteByte, 1);''',
     '''  if(pspTiming) pipeDrain(), pipeItem(Cost::PaletteLoad + bytes * Cost::PaletteByte, 1),
    geterms::add("B", "PaletteLoad", 1), geterms::add("B", "PaletteByte", bytes);''')

#the front end: a PRIM's own, its vertices, their lights and weights; jumps, calls, cuts, patches; words (per run)
edit("ares/psp/ge/draw.cpp", '''  pipe.pending += pipe.vertices + Cost::Prim;
  pipe.vertices = 0;''', '''  pipe.pending += pipe.vertices + Cost::Prim;
  pipe.vertices = 0;
  geterms::add("F", "Prim", 1);''')
edit("ares/psp/ge/draw.cpp", '''    pipe.vertices += format.indexFormat ? count * Cost::IndexedVertex : count * each;''',
     '''    pipe.vertices += format.indexFormat ? count * Cost::IndexedVertex : count * each;
    if(format.indexFormat) geterms::add("F", "IndexedVertex", count);
    else if(format.size == 3) geterms::add("F", "Vertex3", count);
    else if(format.size * Cost::VertexByte < Cost::VertexFloor) geterms::add("F", "VertexFloor", count);
    else geterms::add("F", "VertexByte", f64(count) * format.size);''')
edit("ares/psp/ge/draw.cpp", '''      f64 each = t.weights * Cost::Weight;
      for(auto& light : t.lights) {
        if(!t.lighting || !light.enabled) continue;
        if(!light.directional) each += Cost::PointLight;
        if(light.specular) each += Cost::SpecularLight;
      }''', '''      f64 each = t.weights * Cost::Weight;
      geterms::add("F", "Weight", f64(count) * t.weights);
      for(auto& light : t.lights) {
        if(!t.lighting || !light.enabled) continue;
        if(!light.directional) each += Cost::PointLight, geterms::add("F", "PointLight", count);
        if(light.specular) each += Cost::SpecularLight, geterms::add("F", "SpecularLight", count);
      }''')
edit("ares/psp/ge/list.cpp", "pipeFront(Cost::Jump);", "pipeFront(Cost::Jump), geterms::add(\"F\", \"Jump\", 1);", count=2)
edit("ares/psp/ge/list.cpp", "pipeFront(Cost::Call);", "pipeFront(Cost::Call), geterms::add(\"F\", \"Call\", 1);", count=2)
edit("ares/psp/ge/transform.cpp", "  pipeFront(Cost::Cut);\n", "  pipeFront(Cost::Cut);\n  geterms::add(\"F\", \"Cut\", 1);\n")
edit("ares/psp/ge/curves.cpp", '''    pipe.pending += pipe.vertices + Cost::Prim + Cost::Patch, pipe.vertices = 0;''',
     '''    pipe.pending += pipe.vertices + Cost::Prim + Cost::Patch, pipe.vertices = 0;
    geterms::add("F", "Prim", 1), geterms::add("F", "Patch", 1);
    auto termsKept = geterms::counts();''')
edit("ares/psp/ge/curves.cpp", '''    pipe.vertices = f64(columns * rows) * Cost::PatchVertex, pipe.left = vertices.size();
    drawVertices(drawn, format, vertices, columns * 2);
    if(culls && owedPixels == pixels) pipe = kept;''', '''    pipe.vertices = f64(columns * rows) * Cost::PatchVertex, pipe.left = vertices.size();
    geterms::add("F", "PatchVertex", f64(columns * rows));
    drawVertices(drawn, format, vertices, columns * 2);
    if(culls && owedPixels == pixels) pipe = kept, geterms::counts() = termsKept;''')
edit("ares/psp/kernel/ge.cpp", '''  if(gePspTiming) {  //(never with the GE on its thread: System::threadGE())
    ge.pipeStop(commands);''', '''  if(gePspTiming) {  //(never with the GE on its thread: System::threadGE())
    geterms::add("F", "Command", f64(commands));
    geterms::run();
    ge.pipeStop(commands);''')
edit("ares/psp/kernel/ge.cpp", '''  gePipeAt = std::max(geDoneAt, cycles);
  ge.pipeStart();''', '''  gePipeAt = std::max(geDoneAt, cycles);
  ge.pipeStart();
  geterms::line("S", "");''')
edit("ares/psp/kernel/ge.cpp", "auto Kernel::geEnqueue(bool head) -> void {\n", r'''auto Kernel::geEnqueue(bool head) -> void {
  {
    char t[96];
    std::snprintf(t, sizeof t, "%08x %08x %d %d", arg(0), arg(1), s32(arg(2)), head ? 1 : 0);
    geterms::line("Q", t);
  }
''')
edit("ares/psp/kernel/ge.cpp", "auto Kernel::sceGeListUpdateStallAddr() -> void {\n",
     "auto Kernel::sceGeListUpdateStallAddr() -> void {\n  geterms::line(\"U\", \"\");\n")
edit("ares/psp/kernel/io.cpp", "auto Kernel::sceIoOpen() -> void {\n", '''auto Kernel::sceIoOpen() -> void {
  if(arg(1) & 0x0200) geterms::line("O", memory.readString(arg(0), 1024));
''')
print("ge-terms.py: %s counts the GE model's terms (PHOBOS_GE_TERMS)" % src)
