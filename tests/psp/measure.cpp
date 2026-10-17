//tools/psp-measure's program (see its main.c) in Phobos's core, driven through its menu as a person would. With
//PSP_TEST_PROGRAMS holding pspmeasure.elf, its GE tests must run to the end and write every file, start afresh (the
//first files kept in results-1), run again and draw the same, and skip what's done on a third run; the GE's rounds
//3 to 8 and the FPU probes must write theirs (round 4's stall check getting past a list the GE doesn't finish, its
//counts running again over what the program before left of them, not over what this one leaves; round 6's and
//round 8's timing records complete, every run finished); then it must leave. With PSP_GE_RESULTS set to the results
//folder (results/ge) a real PSP (or another emulator) wrote, each file is compared with that folder's, and what
//differs is listed rather than failed: finding it is what the program is for. PSP_GE_OURS, when set, is a folder this
//core's files are copied to, for a closer look.
#include "kernel-machine.hpp"

#include <algorithm>
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

//Round 6's pictures (part 66: lighting's products and sweeps, the reciprocal read off texels, texture coordinates far
//past the texture, PATCH_CULL_ENABLE, patch divisions past 64 along v).
static const MeasureFile measureFiles6[] = {
  {"light-products-0", 256, 256}, {"light-products-1", 256, 256}, {"light-products-2", 256, 256},
  {"light-products-3", 256, 256}, {"light-sweep-0", 256, 256}, {"light-sweep-1", 256, 256},
  {"light-sweep-2", 256, 256}, {"light-sweep-3", 256, 256}, {"light-sweep-4", 256, 256}, {"light-sweep-5", 256, 256},
  {"light-sweep-6", 256, 256}, {"light-sweep-7", 256, 256}, {"light-sum", 256, 256}, {"recip-pixels", 256, 256},
  {"recip-projected", 256, 256}, {"recip-vertex-0", 256, 256}, {"recip-vertex-1", 256, 256},
  {"recip-vertex-2", 256, 256}, {"recip-sprites", 256, 256}, {"texel-range", 256, 256}, {"patch-cull", 256, 256},
  {"patch-cull-edges", 256, 256},
  {"curves-count-v", 256, 256}, {"curves-count-v-65", 256, 256}, {"curves-count-v-100", 256, 256},
  {"curves-count-v-128", 256, 256}, {"curves-count-v-200", 256, 256}, {"curves-count-v-255", 256, 256},
};

//Round 7's pictures (part 78: anti-aliased lines in every octant, with vertex alphas, blend modes, textures, in 3D).
static const MeasureFile measureFiles7[] = {
  {"aa-slopes", 256, 256}, {"aa-axes", 256, 256}, {"aa-alpha", 256, 256}, {"aa-blend", 256, 256},
  {"aa-textured", 256, 256}, {"aa-3d", 256, 256},
};

//Round 6's timing tests (tools/psp-measure/ge.c's tables): how many records (each case's sizes, at two clock
//settings), runs to a record, times to a run. A record is the case, the size, the clocks (CPU MHz << 16 | bus MHz),
//repeats | values << 16, then the runs' times in microseconds (0xffffffff: a run not finished).
struct TimingFile { const char* name; u32 records, repeats, values; };
static const TimingFile timingFiles6[] = {
  {"time-commands", 50, 16, 2}, {"time-primitives", 186, 16, 2}, {"time-fill", 264, 8, 2}, {"time-sizes", 108, 8, 2},
  {"time-transfers", 68, 8, 2}, {"time-texcache", 224, 8, 2}, {"time-signals", 6, 16, 3},
};
//Round 8's (tools/psp-measure/ge.c's and cpu.c's tables), each at the clock settings it asks for, in this order:
//222/111, 333/166, 222 with the bus at 166 (the PLL at 333), 266/133 MHz (settings' bits 0-3). cpu-clocks has a
//record of its own for each attempt, from 222/111 or 333/166.
struct TimingFile8 { const char* name; u32 records, repeats, values, settings; };
static const TimingFile8 timingFiles8[] = {
  {"time-lists", 80, 16, 3, 15}, {"time-state", 68, 8, 2, 3}, {"time-vertices", 228, 16, 2, 3},
  {"time-setup", 72, 8, 2, 3}, {"time-pixels", 174, 8, 2, 3}, {"time-strips", 280, 8, 2, 3},
  {"time-textures", 100, 8, 2, 3}, {"time-transfers2", 68, 8, 2, 3}, {"time-clears", 48, 8, 2, 3},
  {"time-overlap", 10, 8, 2, 3}, {"time-scenes", 68, 8, 2, 15}, {"cpu-contention", 16, 4, 2, 3},
  {"cpu-alu", 64, 8, 1, 15}, {"cpu-latency", 144, 8, 1, 15}, {"cpu-memory", 104, 4, 1, 15},
  {"cpu-syscalls", 80, 4, 1, 3},
};
static constexpr u32 ClockAttempts8 = 13;
struct TimingRecord { u32 test, size, clocks, repeats, values; std::vector<u32> times; };
//The records, or none if the words don't make whole records.
static auto timingRecords(const std::vector<u32>& words) -> std::vector<TimingRecord> {
  std::vector<TimingRecord> records;
  for(size_t at = 0; at < words.size();) {
    if(words.size() - at < 4) return {};
    TimingRecord record{words[at], words[at + 1], words[at + 2], words[at + 3] & 0xffff, words[at + 3] >> 16, {}};
    size_t count = size_t(record.repeats) * record.values;
    if(words.size() - at - 4 < count) return {};
    record.times.assign(words.begin() + at + 4, words.begin() + at + 4 + count);
    records.push_back(std::move(record));
    at += 4 + count;
  }
  return records;
}
//A record's middle finish time (its runs' first times), unfinished runs counting as the longest.
static auto medianFinish(const TimingRecord& record) -> u32 {
  std::vector<u32> finishes;
  for(u32 run = 0; run < record.repeats; run++) finishes.push_back(record.times[run * record.values]);
  if(finishes.empty()) return 0;
  std::sort(finishes.begin(), finishes.end());
  return finishes[finishes.size() / 2];
}
//ge-statuses's words: part 1's 2 + 1024 * 3 (its looks), part 2's 4 + 128 * 2.
static constexpr u32 StatusLooks = 1024, StatusHeld = 128, StatusWords = 2 + StatusLooks * 3 + 4 + StatusHeld * 2;

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

//How a timing file differs from the reference's: for each case at each clock setting, the middle finish time at each
//size here and there, and the time a unit of size (a straight line through them, least squares).
static auto describeTimes(const char* name, const std::vector<u32>& ours, const std::vector<u32>& theirs) -> void {
  auto here = timingRecords(ours), there = timingRecords(theirs);
  if(here.empty() || there.empty()) {
    std::printf("  %-24s %zu records here, %zu there (not whole records)\n", name, here.size(), there.size());
    return;
  }
  auto slope = [](const std::vector<std::pair<u32, u32>>& points) {
    double n = points.size(), sx = 0, sy = 0, sxx = 0, sxy = 0;
    for(auto [x, y] : points) sx += x, sy += y, sxx += double(x) * x, sxy += double(x) * y;
    double d = n * sxx - sx * sx;
    return d > 0 ? (n * sxy - sx * sy) / d : 0.0;
  };
  for(u32 setting = 0; setting < 2; setting++) {
    std::map<u32, std::vector<std::pair<u32, u32>>> mine, others;
    u32 clocksHere = 0, clocksThere = 0;
    for(u32 n = 0; n < here.size(); n++) {
      if(n * 2 / here.size() != setting) continue;
      mine[here[n].test].push_back({here[n].size, medianFinish(here[n])}), clocksHere = here[n].clocks;
    }
    for(u32 n = 0; n < there.size(); n++) {
      if(n * 2 / there.size() != setting) continue;
      others[there[n].test].push_back({there[n].size, medianFinish(there[n])}), clocksThere = there[n].clocks;
    }
    std::printf("  %-24s at %u/%u MHz here, %u/%u there: case, middle times by size (us), a unit's\n", name,
                clocksHere >> 16, clocksHere & 0xffff, clocksThere >> 16, clocksThere & 0xffff);
    for(auto& [test, points] : mine) {
      std::string line;
      for(auto [size, time] : points) line += " " + std::to_string(time);
      line += " here, ";
      for(auto [size, time] : others[test]) line += std::to_string(time) + " ";
      std::printf("    %2u:%sthere; %.3f here, %.3f there\n", test, line.c_str(), slope(points),
                  slope(others[test]));
    }
  }
}

//How ge-statuses differs: each part's statuses in the order they came (sceGeListSync/sceGeDrawSync in part 1; an
//error code in hex), with when each was first seen (us).
static auto describeStatuses(const std::vector<u32>& ours, const std::vector<u32>& theirs) -> void {
  auto summary = [](const std::vector<u32>& words) {
    auto number = [](u32 word) {  //an error code (below 0) in hex
      char text[16];
      std::snprintf(text, sizeof(text), s32(word) < 0 ? "0x%08x" : "%u", word);
      return std::string(text);
    };
    std::string text = "part 1:";
    u32 looks = std::min(words[0], StatusLooks);
    for(u32 n = 0; n < looks; n++) {
      if(n && words[3 + n * 3] == words[n * 3] && words[4 + n * 3] == words[1 + n * 3]) continue;
      text += " " + std::to_string(words[2 + n * 3]) + "us:" + number(words[3 + n * 3]) + "/" +
              number(words[4 + n * 3]);
    }
    text += ", finished " + number(words[1]) + "us; part 2:";
    const u32* part2 = words.data() + 2 + StatusLooks * 3;
    looks = std::min(part2[0], StatusHeld);
    for(u32 n = 0; n < looks; n++) {
      if(n && part2[5 + n * 2] == part2[3 + n * 2]) continue;
      text += " " + std::to_string(part2[4 + n * 2]) + "us:" + number(part2[5 + n * 2]);
    }
    text += ", held off " + std::to_string(part2[1]) + "us, then " + number(part2[2]) + ", done " +
            std::to_string(part2[3]) + "us";
    return text;
  };
  if(ours.size() != StatusWords || theirs.size() != StatusWords) {
    std::printf("  %-24s %zu words here, %zu there\n", "ge-statuses", ours.size(), theirs.size());
    return;
  }
  std::printf("  %-24s here %s\n  %-24s there %s\n", "ge-statuses", summary(ours).c_str(), "", summary(theirs).c_str());
}

//The GE's reciprocal as the core takes it (docs/psp-core.md, part 75: 128 chords, each the straight line whose worst
//error is least, chord 0 starting at exactly 1), for q in [0.5, 1) given as its 13 or 14 fraction bits: the chord's
//16-bit value (q's reciprocal in units of 2^-15). (measured6.cpp checks each chord against the true reciprocal.)
static auto chordValue(u32 fraction, u32 bits) -> u32 {
  u32 chord = fraction >> (bits - 7), at = fraction << (15 - bits) & 255;
  auto [start, slope] = GE::reciprocalChord(chord);
  return u32((64 * start + 63 + slope * s32(at)) >> 7);
}

//The FPU probes' files (tools/psp-measure/vfpu.c), 16 bytes each: a, b, the result, FCSR after.
static const char* probeFiles[] = {
  "probe-div-zero", "probe-mul-big", "probe-sqrt-negative", "probe-add-infinity", "probe-add-qnan", "probe-add-snan",
  "probe-cvt-minus-2p31", "probe-cvt-infinity", "probe-cvt-qnan", "probe-cvt-snan", "probe-cvt-2p31", "probe-mul-tiny",
  "probe-add-denormal", "probe-cvt-denormal", "probe-add-denormal-fs", "probe-mul-tiny-fs", "probe-cvt-denormal-fs",
};

//The menu's lines (tools/psp-measure/main.c), counted from the top, and the buttons that move through it.
enum : u32 {
  GeRound8Line = 0, GeRound7Line = 1, GeRound6Line = 2, GeRound5Line = 3, GeRound4Line = 4, GeRound3Line = 6,
  ProbesLine = 7, GeRound2Line = 9, AfreshLine = 11, LeaveLine = 12
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

//Round 6's files as this core writes them, for measured6.cpp's comparisons with the owner's PSP: the measure test's
//run hands its over; otherwise the program's round 6 alone (its menu's second line) runs once, in
//a machine of its own, the first time they're asked for. None without the program.
static std::unique_ptr<HostFolder> round6Kept;
static auto keepRound6(const std::filesystem::path& results) -> void {
  round6Kept = std::make_unique<HostFolder>();
  std::vector<std::string> names = {"ge-statuses", "manifest6"};
  for(auto& file : measureFiles6) names.push_back(file.name);
  for(auto& file : timingFiles6) names.push_back(file.name);
  for(auto& name : names) {
    auto file = name == "manifest6" ? name + ".txt" : name + ".bin";
    std::error_code error;
    std::filesystem::copy_file(results / file, round6Kept->path / file, error);
  }
}
auto round6Files() -> std::filesystem::path {
  if(round6Kept) return round6Kept->path;
  auto program = testProgram("pspmeasure.elf");
  if(program.empty()) return {};
  HostFolder stick;
  auto game = stick.path / "PSP/GAME/PSPMEASURE";
  std::filesystem::create_directories(game);
  KernelMachine m;
  m.system.recompiler.enabled = true;
  m.kernel.mount("ms0", stick.path.string());
  std::string error;
  bool loaded = m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/PSPMEASURE/EBOOT.PBP", error);
  CHECK(loaded, true);
  if(!loaded) return {};
  auto results = game / "results" / "ge";
  move(m, 0, GeRound6Line);
  press(m, Cross);
  CHECK(waitFor(m, 20000, [&] { return std::filesystem::exists(results / "curves-count-v-255.bin"); }), true);
  keepRound6(results);
  return round6Kept->path;
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
  //the GE's round 6, its patch divisions past 64 along v last (drawn, as on the PSP: none stalls)
  move(m, GeRound5Line, GeRound6Line);
  press(m, Cross);
  CHECK(waitFor(m, 20000, exists(results / "curves-count-v-255.bin")), true);
  press(m, Cross);
  onlyStallCheckStalled();
  keepRound6(results);
  //the GE's round 7
  move(m, GeRound6Line, GeRound7Line);
  press(m, Cross);
  CHECK(waitFor(m, 3000, exists(results / "aa-3d.bin")), true);
  press(m, Cross);
  onlyStallCheckStalled();
  //the GE's round 8, and the CPU's
  move(m, GeRound7Line, GeRound8Line);
  press(m, Cross);
  CHECK(waitFor(m, 20000, exists(results / "cpu-clocks.bin")), true);
  press(m, Cross);
  onlyStallCheckStalled();
  //leaving
  move(m, GeRound8Line, LeaveLine);
  press(m, Cross);
  CHECK(waitFor(m, 100, [&] { return m.kernel.exited; }), true);

  const char* reference = std::getenv("PSP_GE_RESULTS");
  if(!reference || !*reference) skip("ge-round3", "PSP_GE_RESULTS isn't set: the files compared with nothing");
  const char* copy = std::getenv("PSP_GE_OURS");
  if(copy) std::filesystem::create_directories(copy);
  //each file against the reference's; one the reference lacks is said to be missing (one given up on there, say),
  //but for a round's (3 to 7) when the reference has none of that round (its manifest3.txt to manifest7.txt), as a
  //PSP that hasn't run it
  bool referenceRound3 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest3.txt");
  if(reference && *reference && !referenceRound3) {
    skip("ge-round3", "PSP_GE_RESULTS has no round 3 (manifest3.txt): its files compared with nothing");
  }
  bool referenceRound4 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest4.txt");
  bool referenceRound5 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest5.txt");
  bool referenceRound6 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest6.txt");
  bool referenceRound7 = reference && std::filesystem::exists(std::filesystem::path(reference) / "manifest7.txt");
  auto look = [&](const MeasureFile& file, const std::vector<u32>& ours, bool expected) {
    std::string name = std::string(file.name) + ".bin";
    if(reference && std::filesystem::exists(std::filesystem::path(reference) / name)) {
      auto theirs = readWords(std::filesystem::path(reference) / name);
      describe(file, ours, theirs);
      //(the depth buffer as each of VRAM's copies shows it: the GE's own order, memory.hpp's depthCopy(), exactly)
      if(std::string(file.name).rfind("depth-layout-", 0) == 0) CHECK(ours == theirs, true);
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
  for(auto& file : measureFiles6) {
    auto ours = readWords(results / (std::string(file.name) + ".bin"));
    CHECK(ours.size(), file.width * file.height);
    look(file, ours, referenceRound6);
  }
  //round 6's timing records: whole, as many as the program's tables give, the first half at the clocks the core
  //starts at and the second at 333/166 MHz, every run finished (the core's GE doesn't stall), a SIGNAL's callback
  //before the list's finish
  for(auto& file : timingFiles6) {
    std::string name = std::string(file.name) + ".bin";
    auto ours = readWords(results / name);
    CHECK(ours.size(), file.records * (4 + file.repeats * file.values));
    auto records = timingRecords(ours);
    CHECK(records.size(), file.records);
    bool shaped = true, finished = true, signalled = true;
    for(u32 n = 0; n < records.size(); n++) {
      auto& record = records[n];
      shaped &= record.repeats == file.repeats && record.values == file.values;
      shaped &= record.clocks == (n < records.size() / 2 ? (222u << 16 | 111) : (333u << 16 | 166));
      for(u32 run = 0; run < record.repeats; run++) {
        u32 finish = record.times[run * record.values];
        finished &= finish != 0xffff'ffff;
        if(record.values == 3) signalled &= record.times[run * 3 + 2] <= finish;
      }
    }
    CHECK(shaped, true);
    CHECK(finished, true);
    CHECK(signalled, true);
    if(reference && std::filesystem::exists(std::filesystem::path(reference) / name)) {
      describeTimes(file.name, ours, readWords(std::filesystem::path(reference) / name));
    } else if(referenceRound6) {
      std::printf("  %-24s not in the reference\n", file.name);
    }
    if(copy) std::filesystem::copy_file(results / name, std::filesystem::path(copy) / name,
                                        std::filesystem::copy_options::overwrite_existing);
  }
  auto statuses = readWords(results / "ge-statuses.bin");
  CHECK(statuses.size(), StatusWords);
  if(statuses.size() == StatusWords) {
    u32 part2 = 2 + StatusLooks * 3;
    CHECK(statuses[0] > 0 && statuses[0] <= StatusLooks, true);       //part 1 looked
    CHECK(statuses[1] != 0xffff'ffff, true);                           //and its finish callback came
    CHECK(statuses[part2] > 0 && statuses[part2] <= StatusHeld, true);  //part 2 looked
    CHECK(statuses[part2 + 3] != 0xffff'ffff, true);                   //and its list was done
    if(reference && std::filesystem::exists(std::filesystem::path(reference) / "ge-statuses.bin")) {
      describeStatuses(statuses, readWords(std::filesystem::path(reference) / "ge-statuses.bin"));
    }
  }
  if(copy && std::filesystem::exists(results / "ge-statuses.bin")) {
    std::filesystem::copy_file(results / "ge-statuses.bin", std::filesystem::path(copy) / "ge-statuses.bin",
                               std::filesystem::copy_options::overwrite_existing);
  }
  CHECK(std::filesystem::exists(results / "manifest6.txt"), true);
  for(auto& file : measureFiles7) {
    auto ours = readWords(results / (std::string(file.name) + ".bin"));
    CHECK(ours.size(), file.width * file.height);
    look(file, ours, referenceRound7);
  }
  CHECK(std::filesystem::exists(results / "manifest7.txt"), true);
  //round 8's records: whole, as many as the program's tables give, at each test's settings in turn (as many records
  //at each, the clocks the core grants every one asked for), every run finished (time-lists' third time, where a
  //case has one, no later than its finish), the CPU's loops taking time (its system calls may take none here)
  for(auto& file : timingFiles8) {
    std::string name = std::string(file.name) + ".bin";
    auto ours = readWords(results / name);
    CHECK(ours.size(), file.records * (4 + file.repeats * file.values));
    auto records = timingRecords(ours);
    CHECK(records.size(), file.records);
    static const u32 settingClocks[4] = {222u << 16 | 111, 333u << 16 | 166, 222u << 16 | 166, 266u << 16 | 133};
    std::vector<u32> clocks;
    for(u32 setting = 0; setting < 4; setting++) {
      if(file.settings >> setting & 1) clocks.push_back(settingClocks[setting]);
    }
    bool shaped = true, finished = true, ordered = true, timed = true;
    for(u32 n = 0; n < records.size(); n++) {
      auto& record = records[n];
      shaped &= record.repeats == file.repeats && record.values == file.values;
      shaped &= record.clocks == clocks[n * clocks.size() / records.size()];
      for(u32 run = 0; run < record.repeats; run++) {
        u32 finish = record.times[run * record.values];
        finished &= finish != 0xffff'ffff;
        u32 third = record.values == 3 ? record.times[run * 3 + 2] : 0xffff'ffff;
        if(third != 0xffff'ffff) ordered &= third <= finish;
        for(auto loops : {"cpu-alu", "cpu-latency", "cpu-memory"}) {
          timed &= std::string(file.name) != loops || finish > 0;
        }
      }
    }
    CHECK(shaped, true);
    CHECK(finished, true);
    CHECK(ordered, true);
    CHECK(timed, true);
    if(copy) std::filesystem::copy_file(results / name, std::filesystem::path(copy) / name,
                                        std::filesystem::copy_options::overwrite_existing);
  }
  //cpu-clocks: an attempt a record (1 run of 4 words: what the call gave back, the CPU's and the bus's MHz after, the
  //loop's time), each from 222/111 or 333/166; the first, 333/333/166, granted
  auto attempts = timingRecords(readWords(results / "cpu-clocks.bin"));
  CHECK(attempts.size(), ClockAttempts8);
  if(attempts.size() == ClockAttempts8) {
    bool shaped = true;
    for(auto& attempt : attempts) {
      shaped &= attempt.repeats == 1 && attempt.values == 4 && attempt.times[3] > 0;
      shaped &= attempt.clocks == (222u << 16 | 111) || attempt.clocks == (333u << 16 | 166);
    }
    CHECK(shaped, true);
    CHECK(attempts[0].times[0] == 0 && attempts[0].times[1] == 333 && attempts[0].times[2] == 166, true);
  }
  CHECK(std::filesystem::exists(results / "manifest8.txt"), true);
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
  //round 6's layouts read what they're meant to, in the core's own arithmetic: light-products-0, lit straight on
  //(the share 256), is (2 light + 1)(2 material + 1) >> 10 in each channel, row y's light (y, 255 - y, y) on column x's
  //material (x, x, 255 - x); recip-pixels' first case (w 1 to 2 along u, rows 0-31) and its last (the same along v,
  //rows 224-255), and recip-projected's first (rows 0-63), show the chord's value's low 8 bits at the q each pixel's
  //middle samples (pixels whose middle is on a strip's end left out), and recip-projected's rows 192-223 are
  //recip-pixels' rows 0-31
  auto products = readWords(results / "light-products-0.bin");
  if(products.size() == 256 * 256) {
    bool lit = true;
    for(u32 y = 0; y < 256; y++) {
      for(u32 x = 0; x < 256; x++) {
        u32 light[3] = {y, 255 - y, y}, material[3] = {x, x, 255 - x};
        for(u32 c = 0; c < 3; c++) {
          lit &= (products[y * 256 + x] >> c * 8 & 0xff) == (2 * light[c] + 1) * (2 * material[c] + 1) >> 10;
        }
      }
    }
    CHECK(lit, true);
  }
  auto chords = readWords(results / "recip-pixels.bin");
  if(chords.size() == 256 * 256) {
    bool read = true;
    for(u32 row : {0u, 224u}) {
      for(u32 r = 0; r < 32; r++) {
        s32 left = (r & 16 ? -256 * 16 : 0) + s32(r & 15) - 8;  //the strip's left end, in sixteenths
        for(u32 x = 0; x < 256; x++) {
          s32 middle = s32(x) * 16 + 8, q = 16384 - (middle - left);
          if(middle <= left || q <= 8192) continue;
          u32 expected = q == 16384 ? 0 : chordValue(u32(q - 8192), 13) & 0xff;
          read &= (chords[(row + r) * 256 + x] >> (row ? 8 : 0) & 0xff) == expected;
        }
      }
    }
    CHECK(read, true);
  }
  auto projected = readWords(results / "recip-projected.bin");
  if(projected.size() == 256 * 256) {
    bool read = true;
    for(u32 r = 0; r < 64; r++) {
      s32 left = -256 * 16 * s32(r >> 4) + s32(r & 15) - 8;
      for(u32 x = 0; x < 256; x++) {
        s32 middle = s32(x) * 16 + 8, q = 32767 - (middle - left);
        if(middle <= left || q < 16384) continue;
        read &= (projected[r * 256 + x] & 0xff) == (chordValue(u32(q - 16384), 14) & 0xff);
      }
    }
    CHECK(read, true);
    //its rows 192-223 (the texture matrix's s 128 w and q 1, w 1 to 2) as recip-pixels' rows 0-31
    if(chords.size() == 256 * 256) {
      CHECK(std::equal(chords.begin(), chords.begin() + 32 * 256, projected.begin() + 192 * 256), true);
    }
  }
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

auto measureTests() -> Tests {
  return {
    {"psp measure", pspMeasure},
  };
}

}
