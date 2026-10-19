#pragma once

//Where the PSP's two busiest threads run with its "GE Thread" On, on a phone whose cores aren't alike (big.LITTLE),
//where asked (off by default): the GE's own thread, which draws (the core names it psp-ge-thread), on the fastest core
//the app may run on, and the emulation thread on the next fastest. The cores are ranked by the capacity the kernel
//gives them (cpu_capacity, else cpuinfo_max_freq; nothing is placed where neither is there for every core), among
//those the placing thread may run on as it starts: the app's cpuset, which may leave a core out (the RP6's left out its
//core 5). Asked for again every frame, as Android resets affinities when it moves the app between cpusets; a core
//refused (one the SoC's core control keeps parked, or out of the set now) is passed over for the next. On the RP6 the
//GE's thread on the prime core (7) and the emulation thread on a mid one (4) ran fastest of seven placements; this
//puts them on 7 and 3, its twin. The app's emulation loop (PhobosRunner.cpp) and the runner's measurements
//(tools/psp-runner/android/measure.hpp) share it.

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#if defined(__linux__)
  #include <dirent.h>
  #include <sched.h>
  #include <unistd.h>
#endif

namespace phobos_placement {

#if defined(__linux__)
//(a number from one of the kernel's files: 0 where there's none)
inline auto readNumber(const char* path) -> long long {
  long long value = 0;
  if(FILE* file = std::fopen(path, "r")) {
    if(std::fscanf(file, "%lld", &value) != 1) value = 0;
    std::fclose(file);
  }
  return value;
}

//The CPUs a thread may run on now (tid 0: this one).
inline auto allowedCpus(long tid = 0) -> std::vector<int> {
  cpu_set_t set;
  CPU_ZERO(&set);
  std::vector<int> cpus;
  if(sched_getaffinity((pid_t)tid, sizeof(set), &set)) return cpus;
  for(int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
    if(CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
  }
  return cpus;
}

//The allowed CPUs, fastest first: by cpu_capacity, else by cpuinfo_max_freq, where every one of them has it (ties:
//the lower numbered first: the RP6's kernel gives its A715s, 3 and 4, and its A710s, 5 and 6, the same capacity, and
//the A715 is the newer core); none where neither is there for all of them.
inline auto rankedCpus(const std::vector<int>& allowed) -> std::vector<int> {
  for(const char* format : {"/sys/devices/system/cpu/cpu%d/cpu_capacity",
                            "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq"}) {
    std::vector<std::pair<long long, int>> scored;
    for(int cpu : allowed) {
      char path[96];
      std::snprintf(path, sizeof(path), format, cpu);
      long long score = readNumber(path);
      if(score <= 0) {
        scored.clear();
        break;
      }
      scored.push_back({score, cpu});
    }
    if(scored.empty()) continue;
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::vector<int> cpus;
    for(auto& item : scored) cpus.push_back(item.second);
    return cpus;
  }
  return {};
}

//The GE's own thread's ID, by the name the core gives it (ares/psp/ge/thread.cpp); 0 while there's none.
inline auto geThreadId() -> long {
  long found = 0;
  if(DIR* folder = opendir("/proc/self/task")) {
    while(auto entry = readdir(folder)) {
      char* end = nullptr;
      long tid = std::strtol(entry->d_name, &end, 10);
      if(end == entry->d_name || *end) continue;
      std::string path = std::string("/proc/self/task/") + entry->d_name + "/comm";
      char name[32] = {};
      if(FILE* file = std::fopen(path.c_str(), "r")) {
        if(!std::fgets(name, sizeof(name), file)) name[0] = 0;
        std::fclose(file);
      }
      std::string text = name;
      while(!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
      if(text == "psp-ge-thread") found = tid;
    }
    closedir(folder);
  }
  return found;
}

//(a thread on these CPUs: errno, or 0; tid 0 this one)
inline auto pin(long tid, const std::vector<int>& cpus) -> int {
  cpu_set_t set;
  CPU_ZERO(&set);
  for(int cpu : cpus) {
    if(cpu >= 0 && cpu < CPU_SETSIZE) CPU_SET(cpu, &set);
  }
  return sched_setaffinity((pid_t)tid, sizeof(set), &set) ? errno : 0;
}

struct Placement {
  std::vector<int> allowed, cores;      //(as the placing thread started; cores fastest first)
  long geTid = 0;
  int geCore = -1, emulationCore = -1;  //(where each was put this frame: -1, nowhere)
  unsigned frames = 0;
  unsigned long long refused = 0;       //(a core asked for and refused)

  //As the emulation thread starts, before anything narrows its affinity.
  auto start() -> void {
    allowed = allowedCpus(), cores = rankedCpus(allowed);
    geTid = 0, geCore = emulationCore = -1, frames = 0, refused = 0;
  }
  //Each frame, on the emulation thread: the GE's thread on the fastest core it may have, this thread on the next.
  //False where nothing could be placed (no GE thread, fewer than two ranked cores, or every core refused it): this
  //thread then pinned as it would be without the placement. The GE's thread looked for again every 60 frames (one made
  //as a game loads, or gone as it unloads).
  auto place() -> bool {
    geCore = emulationCore = -1;
    if(cores.size() < 2) return false;
    if(frames++ % 60 == 0) geTid = geThreadId();
    if(!geTid) return false;
    for(int cpu : cores) {
      int error = pin(geTid, {cpu});
      if(!error) {
        geCore = cpu;
        break;
      }
      if(error == ESRCH) return geTid = 0, false;
      refused++;
    }
    if(geCore < 0) return false;
    for(int cpu : cores) {
      if(cpu == geCore) continue;
      if(!pin(0, {cpu})) {
        emulationCore = cpu;
        break;
      }
      refused++;
    }
    return emulationCore >= 0;
  }
  //The GE's thread let go of (anywhere the app may run), where it was placed: the placement turned off.
  auto release() -> void {
    if(geTid && !allowed.empty()) pin(geTid, allowed);
    geTid = 0, geCore = emulationCore = -1;
  }
};
#endif

}
