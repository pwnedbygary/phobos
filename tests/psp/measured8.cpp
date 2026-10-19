//Rounds 8 and 9 of tools/psp-measure against the owner's PSP (docs/psp-core.md, parts 107, 110 and 106): their GE
//timing files as this core writes them (measure.cpp's round8Files() and round9Files(): the measuring program run in
//the core with the option "GE Timing" "PSP") beside the PSP's own (tests/psp/measurements/ge-round8 and ge-round9,
//which run-tests.sh unpacks into PSP_GE_ROUND8 and PSP_GE_ROUND9): each timed list's middle finish time here and on
//the PSP.
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

auto round8Files() -> std::filesystem::path;
auto round9Files() -> std::filesystem::path;

namespace {

struct Round {
  const char* variable;
  const char* group;
  std::filesystem::path (*files)();
};
const Round Round8{"PSP_GE_ROUND8", "ge-round8", round8Files};
const Round Round9{"PSP_GE_ROUND9", "ge-round9", round9Files};

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
auto theirs(const Round& round, const char* name) -> std::vector<u32> {
  const char* folder = std::getenv(round.variable);
  if(!folder || !*folder) {
    skip(round.group, (std::string(round.variable) + " isn't set").c_str());
    return {};
  }
  return present(std::filesystem::path(folder) / (std::string(name) + ".bin"), "the PSP's");
}

//The core's, or nothing without the measuring program (or the PSP's file to compare it with); with both, the core's
//run of the round wrote it.
auto ours(const Round& round, const char* name) -> std::vector<u32> {
  const char* psp = std::getenv(round.variable);
  if(!psp || !*psp || !std::filesystem::exists(std::filesystem::path(psp) / (std::string(name) + ".bin"))) return {};
  auto folder = round.files();
  if(folder.empty()) return {};
  return present(folder / (std::string(name) + ".bin"), "the core's");
}

//A timing file's records (ge.c's timeCases(): the case, the size, the clocks, repeats | values << 16, the runs'
//times), each with its finished runs' middle finish time (0: none finished, as most of round 8's time-lists on the
//PSP: part 107).
struct Timed { u32 test, size, bus; f64 middle; };
auto timed(const std::vector<u32>& w) -> std::vector<Timed> {
  std::vector<Timed> records;
  for(size_t at = 0; at + 4 <= w.size();) {
    u32 repeats = w[at + 3] & 0xffff, values = w[at + 3] >> 16;
    if(w.size() - at - 4 < size_t(repeats) * values) return {};
    std::vector<u32> finishes;
    for(u32 run = 0; run < repeats && values; run++) {
      if(u32 finish = w[at + 4 + run * values]; finish != 0xffff'ffff) finishes.push_back(finish);
    }
    std::sort(finishes.begin(), finishes.end());
    f64 middle = finishes.empty() ? 0 : f64(finishes[finishes.size() / 2]);
    records.push_back({w[at], w[at + 1], w[at + 2] & 0xffff, middle});
    at += 4 + size_t(repeats) * values;
  }
  return records;
}

//A list whose miss is bounded on its own: the test, its case, size and bus clock, and the bound.
struct Key { const char* test; u32 which, size, bus; f64 within; };

//A case the model's texels can't follow, left out as ge-fit.py leaves it out (OUTSIDE): round 8's 3D squares mipmapped
//(time-textures 10-12: level 1 of a 256x256 texture), in perspective (13-15, and time-anchors 6, its 13 again) and
//turned a quarter (16-18).
struct Outside { const char* test; u32 first, last; };
const Outside outside[] = {{"time-textures", 10, 18}, {"time-anchors", 6, 6}};

//Each record of the tests named, here against the PSP (time-lists' and time-lists2's cases 2 and 3 left out: the CPU
//moves their stall addresses, ge-fit.py's rule; and the cases outside the model), its miss checked against a key that
//names it; the misses, or none without the files.
auto misses(const Round& round, std::initializer_list<const char*> tests, std::initializer_list<Key> keys)
  -> std::vector<f64> {
  std::vector<f64> errors;
  for(auto name : tests) {
    auto core = ours(round, name);
    if(core.empty()) return {};
    auto here = timed(core), there = timed(theirs(round, name));
    CHECK(here.size(), there.size());
    if(here.size() != there.size()) continue;
    bool lists = std::string(name) == "time-lists" || std::string(name) == "time-lists2";
    for(u32 n = 0; n < here.size(); n++) {
      CHECK(here[n].test == there[n].test && here[n].size == there[n].size && here[n].bus == there[n].bus, true);
      if(!(there[n].middle > 0) || (lists && (here[n].test == 2 || here[n].test == 3))) continue;
      bool modelled = true;
      for(auto& o : outside) modelled &= std::string(o.test) != name || here[n].test < o.first || here[n].test > o.last;
      if(!modelled) continue;
      f64 error = here[n].middle / there[n].middle - 1;
      errors.push_back(std::abs(error));
      for(auto& key : keys) {
        if(std::string(key.test) != name || key.which != here[n].test || key.size != here[n].size) continue;
        if(key.bus != here[n].bus) continue;
        if(std::abs(error) > key.within) {
          std::printf("  %s case %u size %u: %+.1f%%\n", name, key.which, key.size, error * 100);
        }
        CHECK(std::abs(error) <= key.within, true);
      }
    }
  }
  return errors;
}

//The share of misses within bound.
auto within(const std::vector<f64>& errors, f64 bound) -> f64 {
  return f64(std::count_if(errors.begin(), errors.end(), [&](f64 e) { return e <= bound; })) / errors.size();
}

auto median(std::vector<f64> errors) -> f64 {
  std::sort(errors.begin(), errors.end());
  return errors[errors.size() / 2];
}

//Round 8's GE timing tests (part 107; GE::Cost fitted to rounds 6, 8, 9 and 10 together, part 106): 1,112 lists, half
//within 5.5%, seven in ten within 10%, 91% within 25%, none off by 60%; and lists the refit's terms rest on: a wide
//sprite from swizzled RAM within 6% and a new 1 KiB CLUT8 texture for each sprite within 6% (its texels fetched after
//each TEX_FLUSH; the two took the PSP 4 and 6% longer on round 10's evening, as did many of round 9's textured lists:
//part 106's lists held out), a textured fill within 3%, Super Monkey Ball's CALLed lists within 13%.
auto measuredTime8() -> void {
  auto errors = misses(Round8,
                       {"time-lists", "time-signals2", "time-state", "time-vertices", "time-setup", "time-pixels",
                        "time-strips", "time-textures", "time-transfers2", "time-clears", "time-overlap",
                        "time-scenes"},
                       {{"time-strips", 41, 1, 111, 0.06}, {"time-pixels", 0, 1, 111, 0.03},
                        {"time-textures", 19, 64, 111, 0.06}, {"time-scenes", 5, 1, 111, 0.13}});
  if(errors.empty()) return;
  CHECK(errors.size(), 1112u);
  CHECK(median(errors) <= 0.055, true);
  CHECK(within(errors, 0.10) >= 0.7, true);
  CHECK(within(errors, 0.25) >= 0.91, true);
  CHECK(*std::max_element(errors.begin(), errors.end()) < 0.6, true);
}

//Round 9's (part 110): time-anchors (round 8's cases again, on round 9's day), the triangles, the loads, time-lists2
//and the vertex formats: 882 lists, half within 7.5%, 63% within 10%, nine in ten within 25%, none off by 65%;
//and a new 32x32 CLUT8 texture for each sprite within 11%, a 256x256 one's 32x32 corner within 10% (the
//texels read, not the texture's size), a palette of 256 colors before each sprite within 16%, float and 8-bit
//vertices within 4% (4096 culled triangles), triangles reading narrow from VRAM within 8% and wide from swizzled RAM
//within 6%, and shrunk 2:1 within 13% (the new texture, the wide triangles and the shrink took the PSP 4 to 10%
//longer on round 10's evening; against those runs they're within 6%, 2% and 2%: part 106's lists held out).
auto measuredTime9() -> void {
  auto errors =
    misses(Round9, {"time-anchors", "time-tristrips", "time-loads", "time-lists2", "time-formats"},
           {{"time-loads", 3, 256, 111, 0.11}, {"time-loads", 6, 256, 111, 0.10}, {"time-loads", 45, 256, 111, 0.16},
            {"time-formats", 0, 4096, 111, 0.04}, {"time-formats", 2, 4096, 111, 0.04},
            {"time-tristrips", 16, 1, 111, 0.08}, {"time-tristrips", 5, 1, 111, 0.06},
            {"time-tristrips", 84, 1, 111, 0.13}});
  if(errors.empty()) return;
  CHECK(errors.size(), 882u);
  CHECK(median(errors) <= 0.075, true);
  CHECK(within(errors, 0.10) >= 0.63, true);
  CHECK(within(errors, 0.25) >= 0.9, true);
  CHECK(*std::max_element(errors.begin(), errors.end()) < 0.65, true);
}

}

auto measured8Tests() -> Tests {
  return {{"measured round 8 GE time", measuredTime8}, {"measured round 9 GE time", measuredTime9}};
}

}
