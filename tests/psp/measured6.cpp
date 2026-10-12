//Round 6 of tools/psp-measure against the owner's PSP (docs/psp-core.md, parts 75 and 76): its files as this core
//writes them (measure.cpp's round6Files(): the measuring program itself, run in the core) beside the PSP's own
//(tests/psp/measurements/ge-round6, which run-tests.sh unpacks into PSP_GE_ROUND6). Lighting's products, sweeps and
//sums; the reciprocal at pixels, through the texture matrix, at vertices and on sprites, and its chords read off the
//PSP's pictures; texture coordinates far past the texture; PATCH_CULL_ENABLE and patches at the edges; points added
//up along v; the GE's time over 453 lists of known work, each at 222/111 and 333/166 MHz; and the driver's statuses
//while a list draws. What the core still draws otherwise is pinned, each with what it is.
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

auto round6Files() -> std::filesystem::path;

namespace {

auto words(const std::filesystem::path& path) -> std::vector<u32> {
  std::ifstream file(path, std::ios::binary);
  std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<u32> out(bytes.size() / 4);
  std::memcpy(out.data(), bytes.data(), out.size() * 4);
  return out;
}

//A file that ought to be there: a failure by its name if it's missing (or empty).
auto present(const std::filesystem::path& path, const char* whose) -> std::vector<u32> {
  auto out = words(path);
  if(out.empty()) check(__LINE__, (std::string(whose) + " " + path.filename().string() + " is missing").c_str(), 0, 1);
  return out;
}

//The PSP's file, or nothing without the measurements (which, given, have every file).
auto theirs(const char* name) -> std::vector<u32> {
  const char* folder = std::getenv("PSP_GE_ROUND6");
  if(!folder || !*folder) return {};
  return present(std::filesystem::path(folder) / (std::string(name) + ".bin"), "the PSP's");
}

//The core's, or nothing without the measuring program (or the PSP's file to compare it with); with both, the core's
//run of round 6 wrote it.
auto ours(const char* name) -> std::vector<u32> {
  const char* psp = std::getenv("PSP_GE_ROUND6");
  if(!psp || !*psp || !std::filesystem::exists(std::filesystem::path(psp) / (std::string(name) + ".bin"))) return {};
  auto folder = round6Files();
  if(folder.empty()) return {};
  return present(folder / (std::string(name) + ".bin"), "the core's");
}

//Pixels of two pictures that differ, by whether at (x, y) counts: a picture missing is all apart.
auto apart(const std::vector<u32>& a, const std::vector<u32>& b, std::function<bool(u32, u32)> at = {}) -> u32 {
  if(a.size() != 256 * 256 || b.size() != 256 * 256) return ~0u;
  u32 count = 0;
  for(u32 n = 0; n < a.size(); n++) count += a[n] != b[n] && (!at || at(n % 256, n / 256));
  return count;
}

//light-products-* (every light level on every material level at four normals) and light-sum (one to four lights)
//are the PSP's in every pixel. light-sweep-*'s 8192 normals (ge.c's lightSweep(): normal k of a picture in the 8x8
//block at column k % 32, row k / 32) are but in the few blocks where 256 times the cosine lies within a hundredth of
//a whole level, which the PSP's own arithmetic puts the other side of it (37 normals: lighting.cpp).
auto measuredLighting() -> void {
  for(auto name : {"light-products-0", "light-products-1", "light-products-2", "light-products-3", "light-sum"}) {
    auto psp = theirs(name), core = ours(name);
    if(core.empty()) return;
    CHECK(apart(core, psp), 0u);
  }
  struct Sweep { f64 light[3], base[3], step[3]; };
  const Sweep sweeps[8] = {
    {{0, 0, 1}, {0, 0, 1023}, {1, 0, 0}}, {{0, 0, 1}, {1023, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {0, 0, 3069}, {3, 0, 0}},
    {{0, 0, 1}, {0, 0, 1023.0 / 1024}, {1.0 / 1024, 0, 0}}, {{0, 0, 1}, {0, 0, 1023}, {0, 1, 0}},
    {{1, 0, 0}, {1023, 0, 0}, {0, 1, 0}}, {{1, 1, 1}, {0, 0, 1023}, {1, 0, 0}}, {{0, 0, 3}, {0, 0, 1023}, {1, 0, 0}},
  };
  const u32 pinned[8] = {285, 120, 345, 287, 293, 293, 226, 274};
  for(u32 set = 0; set < 8; set++) {
    char name[32];
    std::snprintf(name, sizeof(name), "light-sweep-%u", set);
    auto psp = theirs(name), core = ours(name);
    if(core.empty()) return;
    auto& s = sweeps[set];
    auto nearLevel = [&](u32 x, u32 y) {
      u32 k = (y >> 3) * 32 + (x >> 3);
      f64 n[3], dot = 0, normal = 0, light = 0;
      for(u32 c = 0; c < 3; c++) {
        n[c] = s.base[c] + k * s.step[c];
        dot += n[c] * s.light[c], normal += n[c] * n[c], light += s.light[c] * s.light[c];
      }
      f64 level = 256 * dot / std::sqrt(normal * light);
      return std::abs(level - std::round(level)) < 0.01;
    };
    CHECK(apart(core, psp), pinned[set]);
    CHECK(apart(core, psp, [&](u32 x, u32 y) { return !nearLevel(x, y); }), 0u);
  }
}

//The reciprocal. First its chords against the PSP's pictures themselves: recip-projected's rows 0-63 (q from
//32767/32768 down a 2^-15 each sixteenth of a pixel, its texel the reciprocal's 16-bit value's low 8 bits; ge.c's
//recipProjected()) read q's reciprocal at every 14-bit q in [0.5, 1) but the top one, the strip's corner, and
//GE::reciprocal() gives each (the value unwrapped from its low byte by the true reciprocal, within 128 of it). Each
//chord is the straight line whose worst error against the true reciprocal is least, but for chord 0, which starts at
//exactly 1: no start or slope a step either way does better. Then the core's pictures against the PSP's: the pixels
//(8 binades, either way along a strip, down v), the texture matrix's q (0 and below too), and 4096 constant-w cells
//as triangles and as 3D sprites, every pixel the PSP's.
auto measuredReciprocal() -> void {
  auto projected = theirs("recip-projected");
  if(projected.size() == 256 * 256) {
    u32 read = 0, matched = 0;
    for(u32 r = 0; r < 64; r++) {
      s32 left = -256 * 16 * s32(r >> 4) + s32(r & 15) - 8;
      for(u32 x = 0; x < 256; x++) {
        s32 middle = s32(x) * 16 + 8, q = 32767 - (middle - left);
        if(middle <= left || q < 16384) continue;
        f64 truth = 1073741824.0 / q;  //(32768 * 32768 / q)
        u32 low = projected[r * 256 + x] & 0xff;
        s64 value = s64(std::round((truth - low) / 256)) * 256 + low;
        u32 bits = 0x3f00'0000 | u32(q - 16384) << 9;  //q as a float in [0.5, 1)
        read++, matched += s64(std::lround(GE::reciprocal(std::bit_cast<float>(bits)) * 32768)) == value;
      }
    }
    CHECK(read, 16383u);  //(each q once: 16 offsets over a window's 4096 sixteenths, 4 windows)
    CHECK(matched, read);
  }
  auto worst = [](u32 chord, s32 start, s32 slope) {
    f64 largest = 0;
    for(s32 at = 0; at < 256; at++) {
      f64 value = (64 * start + 63 + slope * at) >> 7, truth = 2147483648.0 / (32768 + 256 * chord + at);
      largest = std::max(largest, std::abs(value - truth));
    }
    return largest;
  };
  bool least = true;
  for(u32 chord = 0; chord < 128; chord++) {
    auto [start, slope] = GE::reciprocalChord(chord);
    if(chord == 0) {
      least &= start == 131072;
      continue;
    }
    f64 error = worst(chord, start, slope);
    for(s32 a = -2; a <= 2; a++) {
      for(s32 b = -2; b <= 2; b++) least &= (a == 0 && b == 0) || worst(chord, start + a, slope + b) >= error;
    }
  }
  CHECK(least, true);
  for(auto name : {"recip-pixels", "recip-projected", "recip-vertex-0", "recip-vertex-1", "recip-vertex-2",
                   "recip-sprites"}) {
    auto psp = theirs(name), core = ours(name);
    if(core.empty()) return;
    CHECK(apart(core, psp), 0u);
  }
}

//texel-range: through-mode sprites at 2^8 to 2^23 texels either way, each coordinate kept to 15 significant bits.
auto measuredTexelRange() -> void {
  auto psp = theirs("texel-range"), core = ours("texel-range");
  if(core.empty()) return;
  CHECK(apart(core, psp), 0u);
}

//patch-cull's 256 patches (PATCH_CULL_ENABLE, facing, mirroring, normals given or not, culling, lighting) are the
//PSP's; of patch-cull-edges' 64, all but the two cells (56 and 57: points, the right half past the far plane) where
//five vertices at a w near 4 land a row higher on the PSP, their screen positions whole rows, which its own divide
//by w puts a hair above (the core divides exactly; positions aren't round 6's: docs/psp-core.md, part 75).
auto measuredPatchCulling() -> void {
  auto psp = theirs("patch-cull"), core = ours("patch-cull");
  if(core.empty()) return;
  CHECK(apart(core, psp), 0u);
  psp = theirs("patch-cull-edges"), core = ours("patch-cull-edges");
  CHECK(apart(core, psp), 20u);
  CHECK(apart(core, psp, [](u32 x, u32 y) { u32 cell = (y >> 5) * 8 + (x >> 5); return cell != 56 && cell != 57; }),
        0u);
}

//Points added up along v (ge.c's countColumn(): a column's sum is how many vertices it has): 65, 100, 128, 200 and
//255 divisions are the PSP's, every pixel; 16, 63 and 64 too, but where 63 divisions' points fall (column 1: the
//vertices' places that round 4's along u left), each column's points as many as the PSP's.
auto measuredDivisionsAlongV() -> void {
  for(auto name : {"curves-count-v-65", "curves-count-v-100", "curves-count-v-128", "curves-count-v-200",
                   "curves-count-v-255"}) {
    auto psp = theirs(name), core = ours(name);
    if(core.empty()) return;
    CHECK(apart(core, psp), 0u);
  }
  auto psp = theirs("curves-count-v"), core = ours("curves-count-v");
  if(core.empty()) return;
  CHECK(apart(core, psp), 72u);
  CHECK(apart(core, psp, [](u32 x, u32) { return x >> 4 != 1; }), 0u);
  if(core.size() != 256 * 256 || psp.size() != 256 * 256) return;
  bool counted = true;
  for(u32 column = 0; column < 3; column++) {
    u32 here = 0, there = 0;
    for(u32 y = 0; y < 256; y++) {
      for(u32 x = column * 16; x < column * 16 + 16; x++) {
        here += core[y * 256 + x] & 0xff, there += psp[y * 256 + x] & 0xff;
      }
    }
    counted &= here == there;
  }
  CHECK(counted, true);
}

//A timing file's records (ge.c's timeCases(): the case, the size, the clocks, repeats | values << 16, the runs'
//times), each with its runs' middle finish time and middle return from sceGeListEnQueue.
struct Timed { u32 test, size, bus; f64 middle, returned; std::vector<u32> times; };
auto timed(const std::vector<u32>& w) -> std::vector<Timed> {
  std::vector<Timed> records;
  for(size_t at = 0; at + 4 <= w.size();) {
    u32 repeats = w[at + 3] & 0xffff, values = w[at + 3] >> 16;
    if(w.size() - at - 4 < size_t(repeats) * values) return {};
    Timed record{w[at], w[at + 1], w[at + 2] & 0xffff, 0, 0, {}};
    std::vector<u32> finishes, returns;
    for(u32 run = 0; run < repeats && values >= 2; run++) {
      finishes.push_back(w[at + 4 + run * values]);
      returns.push_back(w[at + 5 + run * values]);
    }
    for(u32 n = 0; n < repeats * values; n++) record.times.push_back(w[at + 4 + n]);
    std::sort(finishes.begin(), finishes.end());
    std::sort(returns.begin(), returns.end());
    record.middle = finishes.empty() ? 0 : finishes[finishes.size() / 2];
    record.returned = returns.empty() ? 0 : returns[returns.size() / 2];
    records.push_back(std::move(record));
    at += 4 + size_t(repeats) * values;
  }
  return records;
}

//The GE's time (GE::Cost) against every record of the seven timing tests: each list's middle finish time here and
//on the PSP, 906 of them (both clock settings). As fitted: half within 5%, nearly three quarters within 10%, nine in
//ten within 25%, none off by as much again as it is; and the lists the model's terms rest on: JUMPs, small
//triangles, indexed vertices, four point lights, flat fills into 32- and 16-bit frame buffers and with the depth
//test, a 256x256 copy, a new texture each sprite and the SIGNAL's list within 6%, 65536 NOPs within 20% (the PSP's
//NOP comes cheaper than the other words the command's time rests on) and a strip within 30% (its triangles, sharing
//corners, set up quicker than lone ones). An empty list is done inside sceGeListEnQueue: the PSP's callback came
//inside the call (18 microseconds in at 222 MHz, the call returning at 33), here as the call returns (23 in, its
//reading at most a microsecond before the program's), interrupts being taken once a system function is done.
auto measuredTime() -> void {
  struct Key { const char* test; u32 which, size, bus; f64 within; };
  const Key keys[] = {
    {"time-commands", 1, 65536, 111, 0.2}, {"time-commands", 5, 16384, 111, 0.05},
    {"time-primitives", 0, 4096, 111, 0.06}, {"time-primitives", 2, 4096, 166, 0.3},
    {"time-primitives", 9, 4096, 111, 0.04}, {"time-primitives", 13, 4096, 111, 0.02},
    {"time-fill", 0, 4, 111, 0.02}, {"time-fill", 1, 4, 166, 0.04}, {"time-fill", 27, 4, 111, 0.02},
    {"time-transfers", 0, 256, 111, 0.04}, {"time-texcache", 50, 64, 111, 0.02}, {"time-signals", 0, 16, 166, 0.04},
  };
  std::vector<f64> errors;
  f64 worst = 0;
  for(auto name : {"time-commands", "time-primitives", "time-fill", "time-sizes", "time-transfers", "time-texcache",
                   "time-signals"}) {
    auto core = ours(name);
    if(core.empty()) return;
    auto here = timed(core), there = timed(theirs(name));
    CHECK(here.size(), there.size());
    if(here.size() != there.size()) continue;
    for(u32 n = 0; n < here.size(); n++) {
      CHECK(here[n].test == there[n].test && here[n].size == there[n].size && here[n].bus == there[n].bus, true);
      f64 error = here[n].middle / there[n].middle - 1;
      errors.push_back(std::abs(error));
      worst = std::max(worst, std::abs(error));
      for(auto& key : keys) {
        if(std::string(key.test) != name || key.which != here[n].test || key.size != here[n].size) continue;
        if(key.bus != here[n].bus) continue;
        if(std::abs(error) > key.within) {
          std::printf("  %s case %u size %u: %+.1f%%\n", name, key.which, key.size, error * 100);
        }
        CHECK(std::abs(error) <= key.within, true);
      }
      if(std::string(name) == "time-commands" && here[n].test == 0) {
        CHECK(there[n].middle < there[n].returned, true);
        CHECK(here[n].middle <= here[n].returned && here[n].middle + 1 >= here[n].returned, true);
      }
      //the SIGNAL's callback, before the list's finish, here as on the PSP
      if(here[n].times.size() == 16 * 3) {
        for(u32 run = 0; run < 16; run++) CHECK(here[n].times[run * 3 + 2] <= here[n].times[run * 3], true);
      }
    }
  }
  CHECK(errors.size(), 906u);
  if(errors.empty()) return;
  std::sort(errors.begin(), errors.end());
  auto within = [&](f64 bound) {
    return f64(std::upper_bound(errors.begin(), errors.end(), bound) - errors.begin()) / errors.size();
  };
  CHECK(errors[errors.size() / 2] <= 0.05, true);
  CHECK(within(0.10) >= 0.7, true);
  CHECK(within(0.25) >= 0.9, true);
  CHECK(worst < 1, true);
}

//ge-statuses (ge.c's geStatuses()): a slow list (16 layers of an unswizzled 8888 texture from RAM, half a second on the
//PSP) looked at every 50 microseconds for 1024 looks: drawing (2) to sceGeListSync and sceGeDrawSync alike all along,
//here as on the PSP, its finish callback within a tenth of the PSP's time; and a list GU ends, its 8 such layers
//drawn with interrupts held off for 20 ms: drawing all the while, and still drawing once they're let through, done
//within a tenth of the PSP's time. Its first look comes after GU's nine stall updates (eight sprites and the FINISH),
//22 microseconds in on the PSP, within a few here: the driver's time for each (Kernel::GeStallCycles).
auto measuredStatuses() -> void {
  auto core = ours("ge-statuses"), psp = theirs("ge-statuses");
  constexpr u32 Looks = 1024, Held = 128, Words = 2 + Looks * 3 + 4 + Held * 2;
  if(core.empty()) return;
  CHECK(core.size() == Words && psp.size() == Words, true);
  if(core.size() != Words || psp.size() != Words) return;
  for(auto* w : {&core, &psp}) {
    auto& s = *w;
    bool drawing = true;
    for(u32 n = 0; n < std::min(s[0], Looks); n++) drawing &= s[3 + n * 3] == 2 && s[4 + n * 3] == 2;
    CHECK(drawing, true);
    const u32* part2 = s.data() + 2 + Looks * 3;
    drawing = part2[2] == 2;
    for(u32 n = 0; n < std::min(part2[0], Held); n++) drawing &= part2[5 + n * 2] == 2;
    CHECK(drawing, true);
  }
  CHECK(std::abs(f64(core[1]) / psp[1] - 1) < 0.1, true);
  u32 firstLook = 2 + Looks * 3 + 4;
  CHECK(std::abs(s64(core[firstLook]) - s64(psp[firstLook])) <= 3, true);
  u32 done = 2 + Looks * 3 + 3;
  CHECK(std::abs(f64(core[done]) / psp[done] - 1) < 0.1, true);
}

}

auto measured6Tests() -> Tests {
  return {
    {"measured round 6 lighting", measuredLighting}, {"measured round 6 reciprocal", measuredReciprocal},
    {"measured round 6 texel range", measuredTexelRange}, {"measured round 6 patch culling", measuredPatchCulling},
    {"measured round 6 divisions along v", measuredDivisionsAlongV}, {"measured round 6 GE time", measuredTime},
    {"measured round 6 driver statuses", measuredStatuses},
  };
}

}
