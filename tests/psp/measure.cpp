//tools/psp-ge-measure's program (see its main.c) in Phobos's core. With PSP_TEST_PROGRAMS holding gemeasure.elf, it
//must run to the end (cross pressed to start it and to leave) and write every test's file. With PSP_GE_RESULTS set to
//the results folder a real PSP (or another emulator) wrote, each file is compared with that folder's, and what
//differs is listed rather than failed: finding it is what the program is for. PSP_GE_OURS, when set, is a folder
//this core's files are copied to, for a closer look.
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

//Each test's file and its size in pixels (words).
struct MeasureFile { const char* name; u32 width, height; };
static const MeasureFile measureFiles[] = {
  {"blend-source", 256, 256}, {"blend-destination", 256, 256}, {"blend-inverse", 256, 256},
  {"blend-double", 256, 256}, {"blend-over", 256, 256}, {"blend-subtract", 256, 256}, {"blend-reverse", 256, 256},
  {"blend-min", 256, 256}, {"blend-max", 256, 256}, {"blend-abs", 256, 256}, {"blend-fixed", 256, 256},
  {"blend-factor-11", 256, 256}, {"blend-other-color", 256, 256}, {"blend-destination-alpha", 256, 256},
  {"texfunc-modulate", 256, 256}, {"texfunc-modulate-alpha", 256, 256}, {"texfunc-decal", 256, 256},
  {"texfunc-blend", 256, 256}, {"texfunc-add", 256, 256}, {"texfunc-modulate-2x", 256, 256},
  {"texfunc-decal-2x", 256, 256}, {"texfunc-replace-2x", 256, 256},
  {"filter-magnify", 64, 64}, {"filter-magnify-clamp", 64, 64}, {"filter-magnify-37", 64, 64},
  {"filter-shrink", 64, 64}, {"coverage-sprites", 256, 256}, {"coverage-triangles", 256, 256},
  {"shared-edges", 256, 256}, {"sprite-corners", 32, 8}, {"dither-8888", 256, 256}, {"dither-5650", 256, 256},
  {"stencil-4444-increment", 256, 256}, {"stencil-4444-decrement", 256, 256},
  {"stencil-5551-increment", 256, 256}, {"stencil-5551-decrement", 256, 256}, {"texture-5650", 256, 256},
  {"texture-5551", 256, 256}, {"texture-4444", 256, 256}, {"texture-5551-alpha", 256, 256},
  {"texture-4444-alpha", 256, 256}, {"narrow-5650", 256, 256}, {"narrow-5551", 256, 256}, {"narrow-4444", 256, 256},
  {"gouraud", 256, 256}, {"texels-sprite-shrunk", 256, 256}, {"texels-triangles-shrunk", 256, 256},
  {"texels-sprite-stretched", 256, 256}, {"texels-triangles-stretched", 256, 256}, {"3d-floor-texels", 256, 256},
  {"3d-floor-depth", 256, 256}, {"3d-floor-fog", 256, 256}, {"3d-sprite", 256, 256}, {"3d-rounding", 256, 256},
  {"3d-clip", 256, 256}, {"3d-clip-unclamped", 256, 256}, {"3d-rules", 256, 32}, {"3d-cull", 64, 64},
  {"controller-timing", 48, 1},
};

static auto readWords(const std::filesystem::path& path) -> std::vector<u32> {
  std::ifstream file(path, std::ios::binary);
  std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<u32> words(bytes.size() / 4);
  for(u32 n = 0; n < words.size(); n++) {
    words[n] = bytes[n * 4] | bytes[n * 4 + 1] << 8 | bytes[n * 4 + 2] << 16 | u32(bytes[n * 4 + 3]) << 24;
  }
  return words;
}

//How a file here differs from the reference's: how many pixels, by how much at most in any one byte, and the first
//few. The controller's timing is in microseconds, each read counted as waiting (for most of a frame) or not.
static auto describe(const MeasureFile& file, const std::vector<u32>& ours, const std::vector<u32>& theirs) -> void {
  if(ours.size() != theirs.size()) {
    std::printf("  %-24s %zu words here, %zu there\n", file.name, ours.size(), theirs.size());
    return;
  }
  if(file.height == 1) {
    auto waits = [](const std::vector<u32>& words, u32 group) {
      u32 count = 0;
      for(u32 n = group * 16; n < group * 16 + 16; n++) count += words[n] > 8000;
      return count;
    };
    std::printf("  %-24s of 16, waited here %u, %u, %u; there %u, %u, %u (second latch read, buffer read after a"
                " vertical blank, second buffer read)\n", file.name, waits(ours, 0), waits(ours, 1), waits(ours, 2),
                waits(theirs, 0), waits(theirs, 1), waits(theirs, 2));
    return;
  }
  u32 differing = 0, largest = 0;
  std::string examples;
  for(u32 n = 0; n < ours.size(); n++) {
    if(ours[n] == theirs[n]) continue;
    for(u32 shift : {0, 8, 16, 24}) {
      largest = std::max(largest, u32(std::abs(int(ours[n] >> shift & 0xff) - int(theirs[n] >> shift & 0xff))));
    }
    if(++differing <= 3) {
      char example[80];
      std::snprintf(example, sizeof(example), " (%u, %u: %08x here, %08x there)", n % file.width, n / file.width,
                    ours[n], theirs[n]);
      examples += example;
    }
  }
  if(!differing) std::printf("  %-24s same\n", file.name);
  else std::printf("  %-24s %u of %zu differ, by up to %u in a byte:%s\n", file.name, differing, ours.size(), largest,
                   examples.c_str());
}

static auto geMeasure() -> void {
  auto program = testProgram("gemeasure.elf");
  if(program.empty()) return;
  HostFolder stick;
  std::filesystem::create_directories(stick.path / "PSP/GAME/GEMEASURE");
  KernelMachine m;
  m.system.recompiler.enabled = true;
  m.kernel.mount("ms0", stick.path.string());
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/GEMEASURE/EBOOT.PBP", error), true);
  auto results = stick.path / "PSP/GAME/GEMEASURE/results";
  //cross held to start; once the last file is there, pressed and let go every five frames, since the program waits
  //for it to be let go and then pressed before leaving
  m.kernel.controller.buttons = 0x4000;
  for(u32 frame = 0; frame < 3000 && !m.kernel.exited; frame++) {
    if(frame == 10) m.kernel.controller.buttons = 0;
    if(frame > 10 && std::filesystem::exists(results / "controller-timing.bin")) {
      m.kernel.controller.buttons = frame / 5 % 2 ? 0x4000 : 0;
    }
    m.kernel.run(Kernel::VblankCycles);
  }
  CHECK(m.kernel.exited, true);

  const char* reference = std::getenv("PSP_GE_RESULTS");
  const char* copy = std::getenv("PSP_GE_OURS");
  if(copy) std::filesystem::create_directories(copy);
  for(auto& file : measureFiles) {
    std::string name = std::string(file.name) + ".bin";
    auto ours = readWords(results / name);
    CHECK(ours.size(), file.width * file.height);
    if(reference) describe(file, ours, readWords(std::filesystem::path(reference) / name));
    if(copy && std::filesystem::exists(results / name)) {
      std::filesystem::copy_file(results / name, std::filesystem::path(copy) / name,
                                 std::filesystem::copy_options::overwrite_existing);
    }
  }

  //what the files hold, from end to end: the sprite drawn top-left to bottom-right shows its 4x4 texture (texel n,
  //row n / 4 and column n % 4, is (n + 1) * 0x0f0b07) as it is; the one drawn bottom-left to top-right, turned a
  //quarter: its top row is the texture's last column, top to bottom
  auto corners = readWords(results / "sprite-corners.bin");
  if(corners.size() == 32 * 8) {
    bool upright = true, turned = true;
    for(u32 row = 0; row < 4; row++) {
      for(u32 column = 0; column < 4; column++) {
        upright &= (corners[(2 + row) * 32 + 2 + column] & 0xff'ffff) == (row * 4 + column + 1) * 0x0f'0b07;
        turned &= (corners[(2 + row) * 32 + 18 + column] & 0xff'ffff) == (column * 4 + 3 - row + 1) * 0x0f'0b07;
      }
    }
    CHECK(upright, true);
    CHECK(turned, true);
  }
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

auto measureTests() -> Tests {
  return {
    {"ge measure", geMeasure},
  };
}

}
