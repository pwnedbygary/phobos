//tools/psp-measure's program (see its main.c) in Phobos's core, driven through its menu as a person would. With
//PSP_TEST_PROGRAMS holding pspmeasure.elf, its GE tests must run to the end and write every file, start afresh (the
//first files kept in results-1), run again and draw the same, and skip what's done on a third run; the GE's rounds
//3, 4 and 5 and the FPU probes must write theirs (round 4's stall check getting past a list the GE doesn't finish,
//its counts running again over what the program before left of them, not over what this one leaves); then it must
//leave. With PSP_GE_RESULTS set to the results folder (results/ge) a real PSP (or another emulator) wrote, each file
//is compared with that folder's, and what differs is listed rather than failed: finding it is what the program is
//for. PSP_GE_OURS, when set, is a folder this core's files are copied to, for a closer look.
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>

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
  {"light-diffuse", 256, 256}, {"light-specular", 256, 256}, {"light-spot", 256, 256}, {"light-point", 256, 256},
  {"light-environment", 256, 256}, {"controller-timing", 48, 1},
};

//Round 3's (the depth-layout-* files are the whole 512x256 depth buffer, read through VRAM's four copies).
static const MeasureFile measureFiles3[] = {
  {"light-cosines", 256, 256}, {"light-powered", 256, 256}, {"light-shine", 256, 256},
  {"light-materials", 256, 256}, {"light-colors", 256, 256}, {"light-ambient", 256, 256},
  {"ramp-colors", 256, 256}, {"ramp-colors-vertical", 256, 256}, {"ramp-colors-3d", 256, 256},
  {"ramp-fog", 256, 256}, {"3d-rounding-middle", 256, 256}, {"3d-wall-texels", 256, 256},
  {"3d-sprite-fog", 256, 256}, {"3d-sprite-texels", 256, 256}, {"3d-sprite-flat", 256, 256},
  {"bezier-flat", 256, 256}, {"bezier-curved", 256, 256}, {"bezier-divide-8", 256, 256},
  {"spline-edges-0", 256, 256}, {"spline-edges-3", 256, 256}, {"depth-layout-0", 512, 256},
  {"depth-layout-1", 512, 256}, {"depth-layout-2", 512, 256}, {"depth-layout-3", 512, 256},
};

//Round 4's (part 29: lines, bounding boxes and DXT textures; part 33: curved surfaces; lines-depth and the curves'
//-depths files are depths, read through VRAM's fourth copy).
static const MeasureFile measureFiles4[] = {
  {"lines-shallow", 256, 256}, {"lines-shallow-reversed", 256, 256}, {"lines-steep", 256, 256},
  {"lines-steep-reversed", 256, 256}, {"lines-diagonal", 256, 256}, {"lines-rising", 256, 256},
  {"lines-level", 256, 256}, {"lines-upright", 256, 256}, {"lines-smooth", 256, 256}, {"lines-short", 256, 256},
  {"lines-strips", 256, 256}, {"lines-colors", 256, 256}, {"lines-depth", 256, 256}, {"lines-texels", 256, 256},
  {"lines-3d", 256, 256}, {"bbox", 256, 256}, {"dxt1-colors", 256, 64}, {"dxt3-colors", 256, 64},
  {"dxt5-colors", 256, 64}, {"dxt-layout", 256, 32}, {"curves-bezier", 256, 256}, {"curves-bezier-depths", 256, 256},
  {"curves-places", 256, 256}, {"curves-spline", 256, 256}, {"curves-spline-depths", 256, 256},
  {"curves-texels", 256, 256}, {"curves-made-up", 256, 256}, {"curves-lit", 256, 256}, {"curves-culling", 256, 256},
  {"curves-joins", 256, 256}, {"stall-check", 256, 256}, {"curves-count", 256, 256}, {"curves-count-65", 256, 256},
  {"curves-count-100", 256, 256}, {"curves-count-128", 256, 256}, {"curves-count-200", 256, 256},
  {"curves-count-255", 256, 256},
};

//Round 5's (part 48: the steps on random triangles, the near plane's cut each way, lighting's share, perspective
//texels to the texel in thousands; steps-depth-check is depths, read through VRAM's fourth copy).
static const MeasureFile measureFiles5[] = {
  {"steps-check", 256, 256}, {"steps-depth-check", 256, 256}, {"clip-split", 256, 256}, {"light-share-0", 256, 256},
  {"light-share-1", 256, 256}, {"light-share-2", 256, 256}, {"light-share-3", 256, 256}, {"persp-wall", 256, 256},
  {"persp-divide", 256, 256}, {"persp-w3", 256, 256}, {"persp-floor", 256, 256}, {"persp-sprite", 256, 256},
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

//The FPU probes' files (tools/psp-measure/vfpu.c), 16 bytes each: a, b, the result, FCSR after.
static const char* probeFiles[] = {
  "probe-div-zero", "probe-mul-big", "probe-sqrt-negative", "probe-add-infinity", "probe-add-qnan", "probe-add-snan",
  "probe-cvt-minus-2p31", "probe-cvt-infinity", "probe-cvt-qnan", "probe-cvt-snan", "probe-cvt-2p31", "probe-mul-tiny",
  "probe-add-denormal", "probe-cvt-denormal", "probe-add-denormal-fs", "probe-mul-tiny-fs", "probe-cvt-denormal-fs",
};

//The menu's lines (tools/psp-measure/main.c), counted from the top, and the buttons that move through it.
enum : u32 {
  GeRound5Line = 0, GeRound4Line = 1, GeRound3Line = 3, ProbesLine = 4, GeRound2Line = 6, AfreshLine = 8,
  LeaveLine = 9
};
enum : u32 { Up = 0x0010, Down = 0x0040, Cross = 0x4000 };

//Presses buttons as a person would, times over: let go for ten frames (the program waits for every button to be let
//go before it takes a choice), then held for five.
static auto press(KernelMachine& m, u32 buttons, u32 times = 1) -> void {
  for(u32 n = 0; n < times * 15 && !m.kernel.exited; n++) {
    m.kernel.controller.buttons = n % 15 < 10 ? 0 : buttons;
    m.kernel.run(Kernel::VblankCycles);
  }
  m.kernel.controller.buttons = 0;
}

//Moves the menu's mark from line from to line to.
static auto move(KernelMachine& m, u32 from, u32 to) -> void {
  if(to > from) press(m, Down, to - from);
  if(to < from) press(m, Up, from - to);
}

//Runs with nothing pressed until done() says so or the frames run out; returns done().
static auto waitFor(KernelMachine& m, u32 frames, const std::function<bool()>& done) -> bool {
  for(u32 frame = 0; frame < frames && !done() && !m.kernel.exited; frame++) m.kernel.run(Kernel::VblankCycles);
  return done();
}

static auto pspMeasure() -> void {
  auto program = testProgram("pspmeasure.elf");
  if(program.empty()) return;
  HostFolder stick;
  auto game = stick.path / "PSP/GAME/PSPMEASURE";
  std::filesystem::create_directories(game);
  KernelMachine m;
  m.system.recompiler.enabled = true;
  m.kernel.mount("ms0", stick.path.string());
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/PSPMEASURE/EBOOT.PBP", error), true);
  auto results = game / "results" / "ge", kept = game / "results-1" / "ge", vfpu = game / "results" / "vfpu";
  auto exists = [](std::filesystem::path path) { return [path] { return std::filesystem::exists(path); }; };

  //the GE's tests, then back to the menu (X)
  move(m, 0, GeRound2Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "controller-timing.bin")), true);
  press(m, Cross);
  //starting afresh, which asks first (X again): the files move to results-1
  move(m, GeRound2Line, AfreshLine);
  press(m, Cross);
  press(m, Cross);
  CHECK(waitFor(m, 100, exists(game / "results-1")), true);
  CHECK(std::filesystem::exists(results), false);
  press(m, Cross);
  //the GE's tests again, into a new results folder
  move(m, AfreshLine, GeRound2Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "controller-timing.bin")), true);
  press(m, Cross);
  //and a third time, which finds every test done and writes none of them again
  auto written = std::filesystem::last_write_time(results / "blend-source.bin");
  press(m, Cross);
  waitFor(m, 120, [] { return false; });
  CHECK(std::filesystem::last_write_time(results / "blend-source.bin") == written, true);
  press(m, Cross);
  //the GE's round 3
  move(m, GeRound2Line, GeRound3Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "depth-layout-3.bin")), true);
  press(m, Cross);
  //the FPU probes
  move(m, GeRound3Line, ProbesLine);
  press(m, Cross);
  CHECK(waitFor(m, 600, exists(vfpu / "probe-cvt-denormal-fs.bin")), true);
  press(m, Cross);
  //the GE's round 4, whose stall check leaves the GE a list it can't finish: the wait runs out, the GE is reset and
  //the round goes on. No other test stalls.
  move(m, ProbesLine, GeRound4Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "curves-count-255.bin")), true);
  press(m, Cross);
  std::ifstream stallRecord(results / "stall-check.stalled");
  std::string stalled((std::istreambuf_iterator<char>(stallRecord)), std::istreambuf_iterator<char>());
  CHECK(stalled.find("Reset: sceGuBreak(GU_BREAK_CANCEL) gave 0x00000000, sceGuInit 0x00000000.") != stalled.npos,
        true);
  auto onlyStallCheckStalled = [&] {
    for(auto& entry : std::filesystem::directory_iterator(results)) {
      if(entry.path().extension() == ".stalled") CHECK(entry.path().filename() == "stall-check.stalled", true);
    }
  };
  onlyStallCheckStalled();
  //round 4 again, over its counts as the program from before its GE waits were timed left them on a PSP they froze:
  //run once (.part), twice (.part and .again), given up on (.stopped). Cleared once (curves-count.timed), they run
  //again and draw the same; one finished is kept.
  std::map<std::string, std::vector<u32>> counts;
  for(std::string name : {"curves-count", "curves-count-128", "curves-count-200"}) {
    counts[name] = readWords(results / (name + ".bin"));
    std::filesystem::remove(results / (name + ".bin"));
  }
  for(auto left : {"curves-count.part", "curves-count.again", "curves-count-128.stopped", "curves-count-200.part"}) {
    std::ofstream{results / left};
  }
  auto finished = std::filesystem::last_write_time(results / "curves-count-255.bin");
  CHECK(std::filesystem::remove(results / "curves-count.timed"), true);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "curves-count-200.bin")), true);
  press(m, Cross);
  for(auto& [name, words] : counts) {
    CHECK(readWords(results / (name + ".bin")) == words, true);
    for(auto kind : {".part", ".again", ".stopped"}) CHECK(std::filesystem::exists(results / (name + kind)), false);
  }
  CHECK(std::filesystem::last_write_time(results / "curves-count-255.bin") == finished, true);
  CHECK(std::filesystem::exists(results / "curves-count.timed"), true);
  //and again, over what this program leaves, which isn't cleared: a count that stopped the PSP twice (.part, .again)
  //or while the GE was being reset (.part, .stalled) is given up on; a stall record left by a run that didn't keep
  //its file goes, and the test is drawn again
  auto aside = stick.path / "aside";
  std::filesystem::create_directories(aside);
  for(std::string name : {"curves-count-65.bin", "curves-count-100.bin", "curves-count-200.bin"}) {
    std::filesystem::rename(results / name, aside / name);
  }
  for(auto left : {"curves-count-65.part", "curves-count-65.again", "curves-count-100.part",
                   "curves-count-100.stalled", "curves-count-200.stalled"}) {
    std::ofstream{results / left};
  }
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "curves-count-200.bin")), true);
  press(m, Cross);
  for(std::string name : {"curves-count-65", "curves-count-100"}) {
    CHECK(std::filesystem::exists(results / (name + ".stopped")), true);
    for(auto kind : {".bin", ".part", ".again"}) CHECK(std::filesystem::exists(results / (name + kind)), false);
  }
  CHECK(readWords(results / "curves-count-200.bin") == readWords(aside / "curves-count-200.bin"), true);
  CHECK(std::filesystem::exists(results / "curves-count-200.stalled"), false);
  //put back as they were, for what follows
  for(std::string name : {"curves-count-65", "curves-count-100"}) {
    std::filesystem::remove(results / (name + ".stopped"));
    std::filesystem::rename(aside / (name + ".bin"), results / (name + ".bin"));
  }
  std::filesystem::remove(results / "curves-count-100.stalled");
  //the GE's round 5
  move(m, GeRound4Line, GeRound5Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "persp-sprite.bin")), true);
  press(m, Cross);
  onlyStallCheckStalled();
  //leaving
  move(m, GeRound5Line, LeaveLine);
  press(m, Cross);
  CHECK(waitFor(m, 100, [&] { return m.kernel.exited; }), true);

  const char* reference = std::getenv("PSP_GE_RESULTS");
  const char* copy = std::getenv("PSP_GE_OURS");
  if(copy) std::filesystem::create_directories(copy);
  //each file against the reference's; one the reference lacks is said to be missing (one given up on there, say),
  //but for round 3's, 4's or 5's when the reference has none of that round (its manifest3.txt, manifest4.txt or
  //manifest5.txt), as a PSP that hasn't run it
  bool referenceRound3 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest3.txt");
  bool referenceRound4 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest4.txt");
  bool referenceRound5 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest5.txt");
  auto look = [&](const MeasureFile& file, const std::vector<u32>& ours, bool expected) {
    std::string name = std::string(file.name) + ".bin";
    if(reference && std::filesystem::exists(std::filesystem::path(reference) / name)) {
      describe(file, ours, readWords(std::filesystem::path(reference) / name));
    } else if(reference && expected) {
      std::printf("  %-24s not in the reference\n", file.name);
    }
    if(copy && std::filesystem::exists(results / name)) {
      std::filesystem::copy_file(results / name, std::filesystem::path(copy) / name,
                                 std::filesystem::copy_options::overwrite_existing);
    }
  };
  for(auto& file : measureFiles) {
    std::string name = std::string(file.name) + ".bin";
    auto ours = readWords(results / name);
    CHECK(ours.size(), file.width * file.height);
    //the second run drew what the first did (but the controller's timing, which is measured)
    if(file.height > 1) CHECK(readWords(kept / name) == ours, true);
    look(file, ours, true);
  }
  for(auto& file : measureFiles3) {
    auto ours = readWords(results / (std::string(file.name) + ".bin"));
    CHECK(ours.size(), file.width * file.height);
    look(file, ours, referenceRound3);
  }
  for(auto& file : measureFiles4) {
    auto ours = readWords(results / (std::string(file.name) + ".bin"));
    CHECK(ours.size(), file.width * file.height);
    look(file, ours, referenceRound4);
  }
  for(auto& file : measureFiles5) {
    auto ours = readWords(results / (std::string(file.name) + ".bin"));
    CHECK(ours.size(), file.width * file.height);
    look(file, ours, referenceRound5);
  }
  CHECK(std::filesystem::exists(results / "manifest5.txt"), true);
  for(auto name : probeFiles) CHECK(readWords(vfpu / (std::string(name) + ".bin")).size(), 4);

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
  //the stall check's count before the GE's reset (rows 0-15) and after (16-31): drawn, and alike
  auto stallCheck = readWords(results / "stall-check.bin");
  if(stallCheck.size() == 256 * 256) {
    bool drawn = false, alike = true;
    for(u32 n = 0; n < 16 * 256; n++) {
      drawn |= stallCheck[n] != 0;
      alike &= stallCheck[n] == stallCheck[n + 16 * 256];
    }
    CHECK(drawn, true);
    CHECK(alike, true);
  }
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

auto measureTests() -> Tests {
  return {
    {"psp measure", pspMeasure},
  };
}

}
