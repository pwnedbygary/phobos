//The runner's measurements, for timing a build on a phone: the emulation thread's CPU time and, where the kernel lets
//the user count them, its instructions and cycles; the cores it ran on and their clocks; every thread's CPU time; the
//frames' times; and the app's pinning of its emulation thread, done the same way. build-android.sh includes it ahead
//of runner.cpp (-include) and puts three calls into the runner's frame loop, the same lines in every runner since
//v2.2.0: begin() before the loop's clock starts, frame() after each frame's run, end() after the loop's clock stops.
//Its lines, each beginning "measure", follow the runner's summary (printed at exit); with PHOBOS_MEASURE_JSON, the
//same as JSON ("phobos-psp-runner-measure 1"), written beside its name and renamed.
//
//The emulation thread is the runner's own (root->run() runs the PSP's CPU, the GE's commands and its sound there);
//the GE's drawing threads, the screen's thread and the GPU driver's are counted apart, in the threads' CPU time.
//
//Environment (read at begin()):
//  PHOBOS_PIN          app (the default): as Phobos's emulation thread on Android (PhobosRunner.cpp's
//                      emulationLoop(), "Pin to fastest core" on): nice -10, the last four cores, then the fastest
//                      ones (by cpu_capacity, else cpuinfo_max_freq) asked for again every frame, as the app asks
//                      (refused while the SoC keeps the fastest core parked); fastest: the fastest alone, every frame;
//                      none: left as it starts (and no nice); or a list of CPUs ("7", "4-7", "3,7"), every frame
//  PHOBOS_NICE         the nice value to take (-10 by default, but none with PHOBOS_PIN=none)
//  PHOBOS_GE_PIN       a list of CPUs for the GE's own thread (option "GE Thread" On; the core names it
//                      psp-ge-thread), asked for every frame as the emulation thread's are; unset: left as it is, with
//                      the affinity of the thread that loaded the game. The CPU it was on is kept every frame, either
//                      way
//  PHOBOS_PERF         0: no hardware counters (they're tried by default)
//  PHOBOS_FREQ_EVERY   the emulation thread's CPU's clock read every N frames (30 by default; 0: never)
//  PHOBOS_MEASURE      0: nothing measured, nothing pinned (the runner as it is)
//  PHOBOS_MEASURE_JSON the JSON's file

#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__linux__)
  #include <dirent.h>
  #include <sched.h>
  #include <sys/ioctl.h>
  #include <sys/mman.h>
  #include <sys/syscall.h>
  #include <linux/perf_event.h>
#endif
#if defined(__ANDROID__)
  #include <sys/system_properties.h>
#endif

//The build, for the JSON (build-android.sh names them)
#if !defined(PHOBOS_MEASURE_SOURCE)
  #define PHOBOS_MEASURE_SOURCE "unknown"
#endif
#if !defined(PHOBOS_MEASURE_BUILD)
  #define PHOBOS_MEASURE_BUILD "unknown"
#endif

namespace phobos_measure {

using u64 = unsigned long long;

inline auto environment(const char* name, const char* otherwise = "") -> std::string {
  const char* value = std::getenv(name);
  return value && *value ? value : otherwise;
}

//A small file's text (/proc's and /sys's), whole; false if it can't be read.
inline auto readFile(const std::string& path, std::string& text) -> bool {
  text.clear();
  FILE* file = std::fopen(path.c_str(), "r");
  if(!file) return false;
  char buffer[4096];
  size_t size;
  while((size = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, size);
  std::fclose(file);
  return true;
}

//Its first line, without the line's end or spaces after it; empty if it can't be read.
inline auto readLine(const std::string& path) -> std::string {
  std::string text;
  if(!readFile(path, text)) return {};
  auto end = text.find('\n');
  if(end != std::string::npos) text.resize(end);
  while(!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.pop_back();
  return text;
}

inline auto readNumber(const std::string& path, long long& value) -> bool {
  auto text = readLine(path);
  if(text.empty()) return false;
  char* end = nullptr;
  value = std::strtoll(text.c_str(), &end, 0);
  return end != text.c_str();
}

inline auto steadySeconds() -> double {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline auto clockSeconds(clockid_t clock) -> double {
  timespec time{};
  return clock_gettime(clock, &time) == 0 ? time.tv_sec + time.tv_nsec * 1e-9 : -1.0;
}

inline auto threadId() -> long {
  #if defined(__linux__)
  return (long)syscall(SYS_gettid);
  #else
  return (long)getpid();
  #endif
}

inline auto currentCpu() -> int {
  #if defined(__linux__)
  return sched_getcpu();
  #else
  return -1;
  #endif
}

//"0-2,4,6-7" (the kernel's lists: cpus files, cpusets) as CPUs, and back.
inline auto parseCpus(const std::string& text) -> std::vector<int> {
  std::vector<int> cpus;
  size_t at = 0;
  while(at < text.size()) {
    auto comma = text.find(',', at);
    auto item = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    at = comma == std::string::npos ? text.size() : comma + 1;
    if(item.empty()) continue;
    char* end = nullptr;
    long first = std::strtol(item.c_str(), &end, 10);
    if(end == item.c_str() || first < 0) return {};
    long last = first;
    if(*end == '-') {
      const char* rest = end + 1;
      last = std::strtol(rest, &end, 10);
      if(end == rest || last < first) return {};
    }
    if(*end != 0 || last > 4095) return {};
    for(long cpu = first; cpu <= last; cpu++) cpus.push_back((int)cpu);
  }
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  return cpus;
}

inline auto cpuList(const std::vector<int>& cpus) -> std::string {
  std::string text;
  for(size_t n = 0; n < cpus.size();) {
    size_t last = n;
    while(last + 1 < cpus.size() && cpus[last + 1] == cpus[last] + 1) last++;
    if(!text.empty()) text += ',';
    text += std::to_string(cpus[n]);
    if(last > n) text += '-' + std::to_string(cpus[last]);
    n = last + 1;
  }
  return text;
}

//A string as JSON writes it: quotes, backslashes and control characters escaped, and each byte past ASCII written as
//the character of its value (the names here are the kernel's: threads', PMUs', cgroups').
inline auto jsonText(const std::string& text) -> std::string {
  std::string out = "\"";
  for(unsigned char c : text) {
    if(c == '"' || c == '\\') out += '\\', out += (char)c;
    else if(c < 0x20 || c >= 0x7f) {
      char escaped[8];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      out += escaped;
    } else out += (char)c;
  }
  return out + "\"";
}

inline auto jsonNumber(double value, const char* format = "%.6g") -> std::string {
  if(!(value == value) || value < -1e300 || value > 1e300) return "null";
  char text[64];
  std::snprintf(text, sizeof(text), format, value);
  return text;
}

//A thread's time on the CPU and waiting for one, from its schedstat (in nanoseconds: run, wait, timeslices), else
//its stat's user and system ticks; with the CPU it last ran on (stat's 39th field).
struct TaskTime {
  bool valid = false, schedstat = false;
  u64 run = 0, wait = 0, slices = 0;  //(nanoseconds; without schedstat, run is the ticks' time)
  int lastCpu = -1;
};

inline auto taskTime(const std::string& task) -> TaskTime {
  TaskTime time;
  std::string text;
  if(readFile(task + "/schedstat", text) &&
     std::sscanf(text.c_str(), "%llu %llu %llu", &time.run, &time.wait, &time.slices) == 3) {
    time.valid = time.schedstat = true;
  }
  if(readFile(task + "/stat", text)) {
    auto close = text.rfind(')');
    if(close != std::string::npos) {
      std::vector<std::string> fields;  //from the 3rd (the state) on
      size_t at = close + 1;
      while(at < text.size()) {
        while(at < text.size() && text[at] == ' ') at++;
        auto end = text.find(' ', at);
        if(end == std::string::npos) end = text.size();
        if(end > at) fields.push_back(text.substr(at, end - at));
        at = end;
      }
      if(fields.size() > 36) time.lastCpu = std::atoi(fields[36].c_str());
      if(!time.valid && fields.size() > 12) {
        long ticks = sysconf(_SC_CLK_TCK);
        u64 used = std::strtoull(fields[11].c_str(), nullptr, 10) + std::strtoull(fields[12].c_str(), nullptr, 10);
        if(ticks > 0) time.run = used * 1'000'000'000ull / ticks, time.valid = true;
      }
    }
  }
  return time;
}

struct Thread {
  long tid = 0;
  std::string name;
  TaskTime time;
};

//Every thread of the process now, by its ID.
inline auto threads() -> std::map<long, Thread> {
  std::map<long, Thread> list;
  #if defined(__linux__)
  if(DIR* folder = opendir("/proc/self/task")) {
    while(auto entry = readdir(folder)) {
      char* end = nullptr;
      long tid = std::strtol(entry->d_name, &end, 10);
      if(end == entry->d_name || *end) continue;
      std::string task = std::string("/proc/self/task/") + entry->d_name;
      Thread thread;
      thread.tid = tid;
      thread.name = readLine(task + "/comm");
      thread.time = taskTime(task);
      list[tid] = thread;
    }
    closedir(folder);
  }
  #endif
  return list;
}

//The CPUs: their kind (MIDR_EL1's implementer and part, from sysfs, else /proc/cpuinfo), capacity and top clock.
struct Cpu {
  int id = 0;
  std::string kind;
  long long capacity = 0, maxKHz = 0;
  bool online = true;
};

inline auto cpuKind(unsigned implementer, unsigned part) -> std::string {
  if(implementer == 0x41) {
    static const std::map<unsigned, const char*> arm = {
      {0xd03, "Cortex-A53"}, {0xd04, "Cortex-A35"}, {0xd05, "Cortex-A55"}, {0xd07, "Cortex-A57"},
      {0xd08, "Cortex-A72"}, {0xd09, "Cortex-A73"}, {0xd0a, "Cortex-A75"}, {0xd0b, "Cortex-A76"},
      {0xd0c, "Neoverse N1"}, {0xd0d, "Cortex-A77"}, {0xd40, "Neoverse V1"}, {0xd41, "Cortex-A78"},
      {0xd44, "Cortex-X1"}, {0xd46, "Cortex-A510"}, {0xd47, "Cortex-A710"}, {0xd48, "Cortex-X2"},
      {0xd49, "Neoverse N2"}, {0xd4b, "Cortex-A78C"}, {0xd4d, "Cortex-A715"}, {0xd4e, "Cortex-X3"},
      {0xd4f, "Neoverse V2"}, {0xd80, "Cortex-A520"}, {0xd81, "Cortex-A720"}, {0xd82, "Cortex-X4"},
      {0xd85, "Cortex-X925"}, {0xd87, "Cortex-A725"},
    };
    if(auto found = arm.find(part); found != arm.end()) return found->second;
  }
  char text[40];
  std::snprintf(text, sizeof(text), "implementer 0x%02x part 0x%03x", implementer, part);
  return text;
}

inline auto cpus() -> std::vector<Cpu> {
  std::vector<Cpu> list;
  long count = sysconf(_SC_NPROCESSORS_CONF);
  if(count <= 0) return list;
  //(/proc/cpuinfo lists the online CPUs, each "processor : N" and then its "CPU implementer" and "CPU part")
  std::map<int, std::pair<unsigned, unsigned>> parts;
  std::string text;
  if(readFile("/proc/cpuinfo", text)) {
    int processor = -1;
    size_t at = 0;
    while(at < text.size()) {
      auto end = text.find('\n', at);
      if(end == std::string::npos) end = text.size();
      auto line = text.substr(at, end - at);
      at = end + 1;
      auto colon = line.find(':');
      if(colon == std::string::npos) continue;
      auto key = line.substr(0, line.find_last_not_of(" \t", colon - 1) + 1);
      unsigned long value = std::strtoul(line.c_str() + colon + 1, nullptr, 0);
      if(key == "processor") processor = (int)value;
      else if(key == "CPU implementer" && processor >= 0) parts[processor].first = (unsigned)value;
      else if(key == "CPU part" && processor >= 0) parts[processor].second = (unsigned)value;
    }
  }
  for(int id = 0; id < count; id++) {
    Cpu cpu;
    cpu.id = id;
    std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(id);
    long long online = 1;
    if(readNumber(base + "/online", online)) cpu.online = online != 0;
    long long midr = 0;
    if(readNumber(base + "/regs/identification/midr_el1", midr) && midr) {
      cpu.kind = cpuKind((unsigned)(midr >> 24 & 0xff), (unsigned)(midr >> 4 & 0xfff));
    } else if(auto found = parts.find(id); found != parts.end()) {
      cpu.kind = cpuKind(found->second.first, found->second.second);
    }
    readNumber(base + "/cpu_capacity", cpu.capacity);
    readNumber(base + "/cpufreq/cpuinfo_max_freq", cpu.maxKHz);
    list.push_back(cpu);
  }
  return list;
}

//The fastest CPUs as the app takes them (PhobosRunner.cpp's fastestCpus()): the highest cpu_capacity, else the highest
//cpuinfo_max_freq, where every CPU has one; none otherwise.
inline auto fastestCpus(const std::vector<Cpu>& list) -> std::vector<int> {
  for(int pass = 0; pass < 2; pass++) {
    long long best = 0;
    bool complete = !list.empty();
    for(auto& cpu : list) {
      long long score = pass == 0 ? cpu.capacity : cpu.maxKHz;
      complete = complete && score > 0;
      best = std::max(best, score);
    }
    if(!complete) continue;
    std::vector<int> fastest;
    for(auto& cpu : list) if((pass == 0 ? cpu.capacity : cpu.maxKHz) == best) fastest.push_back(cpu.id);
    return fastest;
  }
  return {};
}

#if defined(__linux__)
inline auto affinity() -> std::vector<int> {
  cpu_set_t set;
  CPU_ZERO(&set);
  if(sched_getaffinity(0, sizeof(set), &set)) return {};
  std::vector<int> cpus;
  for(int cpu = 0; cpu < CPU_SETSIZE; cpu++) if(CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
  return cpus;
}

inline auto setAffinity(const std::vector<int>& cpus, long tid = 0) -> int {  //errno, or 0 (tid 0: this thread)
  cpu_set_t set;
  CPU_ZERO(&set);
  for(int cpu : cpus) if(cpu >= 0 && cpu < CPU_SETSIZE) CPU_SET(cpu, &set);
  return sched_setaffinity((pid_t)tid, sizeof(set), &set) ? errno : 0;
}

inline auto affinityOf(long tid) -> std::vector<int> {
  cpu_set_t set;
  CPU_ZERO(&set);
  if(sched_getaffinity((pid_t)tid, sizeof(set), &set)) return {};
  std::vector<int> cpus;
  for(int cpu = 0; cpu < CPU_SETSIZE; cpu++) if(CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
  return cpus;
}

//(the GE's own thread's ID, by the name the core gives it; 0 while there's none)
inline auto geThreadId() -> long {
  long found = 0;
  if(DIR* folder = opendir("/proc/self/task")) {
    while(auto entry = readdir(folder)) {
      char* end = nullptr;
      long tid = std::strtol(entry->d_name, &end, 10);
      if(end == entry->d_name || *end) continue;
      if(readLine(std::string("/proc/self/task/") + entry->d_name + "/comm") == "psp-ge-thread") found = tid;
    }
    closedir(folder);
  }
  return found;
}
#endif

//The hardware counters: perf_event_open() on this thread alone (pid 0, any CPU), user mode only (what
//perf_event_paranoid 2 allows). Instructions and cycles are a pinned group, counted together all the time; the other
//events (stalls, mispredicts, refills) are a group each, as a phone's kernel may keep most of the PMU's counters for
//its own counting (Qualcomm's governors count every core), too many for five at once: the kernel takes turns with
//what's left, and their counts are scaled from their turns (results()), or not counted where they never had one. A
//phone's cores of each kind may have a PMU of their own (sysfs's armv8_*/armv9_* PMUs, each with its cpus): a
//thread's counter on one counts only while the thread is on that PMU's cores, so each PMU gets its groups and the
//totals add them up. Without such PMUs in sysfs, the kernel's generic events.
struct Counter {
  std::string name;
  int fd = -1;
  u64 value = 0;
};

struct Group {
  std::string pmu, cpus, kind;  //kind: core (instructions, cycles), extra (one other event), software
  std::vector<Counter> counters;
  u64 enabled = 0, running = 0;
  bool read = false;
};

struct Perf {
  bool tried = false, available = false, denied = false;  //denied: perf_event_open refused (said in error)
  std::string error;                 //why there are none (or why some are missing)
  std::vector<std::string> problems; //the counters that didn't open, and why
  std::vector<Group> groups;
};

#if defined(__linux__)
inline auto perfOpen(u64 type, u64 config, int leader, bool excludeKernel, bool pinned) -> int {
  perf_event_attr attr{};
  attr.size = sizeof(attr);
  attr.type = (uint32_t)type;
  attr.config = config;
  attr.disabled = leader < 0;  //(a group starts and stops with its leader)
  attr.pinned = pinned && leader < 0;
  attr.exclude_kernel = excludeKernel;
  attr.exclude_hv = 1;
  attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
  return (int)syscall(__NR_perf_event_open, &attr, 0, -1, leader, PERF_FLAG_FD_CLOEXEC);
}

struct EventName { const char* name; u64 config; };

//A group: its events opened in order, the first its leader; an event that won't open is left out (and said), and a
//leader that won't open leaves the group out.
inline auto perfGroup(Perf& perf, const std::string& pmu, const std::string& cpus, const std::string& kind, u64 type,
                      const std::vector<EventName>& events, bool excludeKernel, bool pinned) -> int {
  Group group;
  group.pmu = pmu, group.cpus = cpus, group.kind = kind;
  for(auto& event : events) {
    int leader = group.counters.empty() ? -1 : group.counters[0].fd;
    int fd = perfOpen(type, event.config, leader, excludeKernel, pinned);
    if(fd < 0) {
      int error = errno;
      perf.problems.push_back(pmu + " " + event.name + ": " + std::strerror(error));
      if(group.counters.empty()) return error;
      continue;
    }
    group.counters.push_back({event.name, fd, 0});
  }
  if(!group.counters.empty()) perf.groups.push_back(group);
  return 0;
}
#endif

//Whether this process may have memory that code runs from, mapped as the recompiler maps its own (nall's
//memory::map(): anonymous, read, write and execute). Where SELinux refuses it (execmem), the recompiler finds no code
//memory and the interpreter runs everything (Allegrex::Recompiler::reset()), unseen but for the speed: errno, or 0.
inline auto executableMemoryRefused() -> int {
  #if defined(__linux__)
  void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if(page == MAP_FAILED) return errno ? errno : EPERM;
  munmap(page, 4096);
  #endif
  return 0;
}

inline auto perfParanoid() -> long long {
  long long level = -1000;
  readNumber("/proc/sys/kernel/perf_event_paranoid", level);
  return level;
}

inline auto property(const char* name) -> std::string {
  #if defined(__ANDROID__)
  char value[PROP_VALUE_MAX] = {};
  if(__system_property_get(name, value) > 0) return value;
  #endif
  (void)name;
  return {};
}

inline auto perfStart(Perf& perf) -> void {
  perf.tried = true;
  #if defined(__linux__)
  //the CPUs' PMUs in sysfs (arm_pmu names them armv8_pmuv3, armv8_pmuv3_N, armv8_cortex_*, armv9_cortex_*)
  struct Pmu { std::string name, cpus; long long type; };
  std::vector<Pmu> pmus;
  if(DIR* folder = opendir("/sys/bus/event_source/devices")) {
    while(auto entry = readdir(folder)) {
      std::string name = entry->d_name;
      bool cpuPmu = name.rfind("armv8", 0) == 0 || name.rfind("armv9", 0) == 0 ||
                    name.find("cortex") != std::string::npos || name.find("neoverse") != std::string::npos;
      if(!cpuPmu) continue;
      std::string base = "/sys/bus/event_source/devices/" + name;
      long long type = -1;
      if(!readNumber(base + "/type", type) || type < 0) continue;
      pmus.push_back({name, readLine(base + "/cpus"), type});
    }
    closedir(folder);
  }
  std::sort(pmus.begin(), pmus.end(), [](auto& a, auto& b) { return a.type < b.type; });
  //ARMv8's common events, by number: INST_RETIRED, CPU_CYCLES (the dedicated cycle counter), then the stalls,
  //mispredicted branches and the level 1 caches' refills
  std::vector<EventName> coreRaw = {{"instructions", 0x08}, {"cycles", 0x11}};
  std::vector<EventName> extraRaw = {{"frontend stall cycles", 0x23}, {"backend stall cycles", 0x24},
                                     {"mispredicted branches", 0x10}, {"L1I refills", 0x01}, {"L1D refills", 0x03}};
  int denied = 0;
  for(auto& pmu : pmus) {
    int error = perfGroup(perf, pmu.name, pmu.cpus, "core", (u64)pmu.type, coreRaw, true, true);
    if(error == EACCES || error == EPERM) { denied = error; break; }
    for(auto& event : extraRaw) perfGroup(perf, pmu.name, pmu.cpus, "extra", (u64)pmu.type, {event}, true, false);
  }
  if(pmus.empty()) {
    std::vector<EventName> core = {{"instructions", PERF_COUNT_HW_INSTRUCTIONS}, {"cycles", PERF_COUNT_HW_CPU_CYCLES}};
    std::vector<EventName> extra = {{"frontend stall cycles", PERF_COUNT_HW_STALLED_CYCLES_FRONTEND},
                                    {"backend stall cycles", PERF_COUNT_HW_STALLED_CYCLES_BACKEND},
                                    {"mispredicted branches", PERF_COUNT_HW_BRANCH_MISSES}};
    int error = perfGroup(perf, "generic", "", "core", PERF_TYPE_HARDWARE, core, true, true);
    if(error == EACCES || error == EPERM) denied = error;
    else for(auto& event : extra) perfGroup(perf, "generic", "", "extra", PERF_TYPE_HARDWARE, {event}, true, false);
  }
  if(!denied) {
    //the kernel's own counts: CPU time and migrations between cores (page faults: the user's)
    std::vector<EventName> software = {{"task clock ns", PERF_COUNT_SW_TASK_CLOCK},
                                       {"migrations", PERF_COUNT_SW_CPU_MIGRATIONS},
                                       {"page faults", PERF_COUNT_SW_PAGE_FAULTS}};
    perfGroup(perf, "software", "", "software", PERF_TYPE_SOFTWARE, software, true, false);
  }
  bool counted = false;
  for(auto& group : perf.groups) counted = counted || group.kind == "core";
  perf.available = counted;
  perf.denied = denied != 0;
  if(denied) {
    //(Android's paranoid level 3, security.perf_harden 1, refuses every user; at 2 or below the kernel allows a
    //thread's own user-mode counts, so a refusal then is SELinux's)
    long long paranoid = perfParanoid();
    std::string harden = property("security.perf_harden");
    std::string why = paranoid == -1000 ? ")"
      : paranoid <= 2 ? "; the kernel allows them at this level, so SELinux (or seccomp) refused them)"
      : !harden.empty() ? "; adb shell setprop security.perf_harden 0 allows them until the next boot)"
      : "; sysctl kernel.perf_event_paranoid=2 allows them)";
    perf.error = std::string("perf_event_open: ") + std::strerror(denied) + " (perf_event_paranoid " +
                 (paranoid == -1000 ? std::string("unreadable") : std::to_string(paranoid)) +
                 (harden.empty() ? std::string() : ", security.perf_harden " + harden) + why;
  } else if(!counted) {
    perf.error = perf.problems.empty() ? "no CPU PMU" : "no instructions or cycles counter opened";
  }
  for(auto& group : perf.groups) {
    ioctl(group.counters[0].fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(group.counters[0].fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  }
  #else
  perf.error = "no perf_event_open on this system";
  #endif
}

inline auto perfStop(Perf& perf) -> void {
  #if defined(__linux__)
  for(auto& group : perf.groups) ioctl(group.counters[0].fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
  for(auto& group : perf.groups) {
    std::vector<u64> data(3 + group.counters.size());
    auto bytes = ::read(group.counters[0].fd, data.data(), data.size() * sizeof(u64));
    if(bytes >= (ssize_t)(3 * sizeof(u64))) {
      group.enabled = data[1], group.running = data[2], group.read = true;
      for(size_t n = 0; n < group.counters.size() && n < data[0]; n++) group.counters[n].value = data[3 + n];
    } else {
      perf.problems.push_back(group.pmu + " " + group.kind + ": no count (" +
                              (bytes < 0 ? std::strerror(errno) : "the group never got the PMU") + ")");
    }
    for(auto& counter : group.counters) close(counter.fd);
  }
  #endif
  (void)perf;
}

//The CPUs' clocks now (each policy's scaling_cur_freq, by its first CPU), in kHz.
inline auto policyClocks(const std::vector<Cpu>& list) -> std::map<int, long long> {
  std::map<int, long long> clocks;
  std::vector<int> seen;
  for(auto& cpu : list) {
    if(std::find(seen.begin(), seen.end(), cpu.id) != seen.end()) continue;
    std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu.id) + "/cpufreq/";
    auto related = parseCpus(readLine(base + "related_cpus"));
    seen.insert(seen.end(), related.begin(), related.end());
    long long khz = 0;
    if(readNumber(base + "scaling_cur_freq", khz)) clocks[cpu.id] = khz;
  }
  return clocks;
}

struct Sample { unsigned frame; int cpu; long long khz; };

struct State {
  bool enabled = true, began = false, ended = false, printed = false;
  long tid = 0;
  std::string jsonFile;
  //the window
  double wallBegin = 0, wallEnd = 0, threadBegin = 0, threadEnd = 0, processBegin = 0, processEnd = 0;
  rusage usageBegin{}, usageEnd{};
  bool threadUsage = false;  //(RUSAGE_THREAD; elsewhere the process's)
  TaskTime taskBegin, taskEnd;
  std::map<long, Thread> threadsBegin, threadsEnd;
  //the frames
  unsigned frames = 0;
  double lastFrame = 0;
  std::vector<float> frameMs;
  std::map<int, unsigned> framesByCpu;
  unsigned freqEvery = 30;
  std::vector<Sample> samples;
  //the CPUs and the pinning
  std::vector<Cpu> cpuList;
  std::map<int, long long> clocksBegin, clocksEnd;
  std::string pinMode;
  std::vector<int> wide, target, affinityBefore, affinityEnd;
  bool niceAsked = false;
  int niceValue = 0, niceBefore = 0, niceAfter = 0, niceError = 0;
  u64 pinAsked = 0, pinRefused = 0;
  int pinError = 0;
  //the GE's own thread (option "GE Thread"), found by its name; its pinning (PHOBOS_GE_PIN) and the CPU it was on
  long geTid = 0;
  std::vector<int> gePin, geAffinityBegin, geAffinityEnd;
  std::map<int, unsigned> geFramesByCpu;
  u64 gePinAsked = 0, gePinRefused = 0;
  int gePinError = 0;
  //the system
  std::string cpuset, cgroup, kernel, android, model, soc, harden;
  long long paranoid = -1000;
  int execRefused = 0;
  Perf perf;
};

inline auto state() -> State& {
  static State instance;
  return instance;
}

inline auto pinFrame(State& s) -> void {
  #if defined(__linux__)
  if(s.target.empty()) return;
  s.pinAsked++;
  if(int error = setAffinity(s.target)) s.pinRefused++, s.pinError = error;
  #else
  (void)s;
  #endif
}

//(the GE's own thread each frame, if there's one: pinned where PHOBOS_GE_PIN asks, and the CPU it was on kept)
inline auto geFrame(State& s) -> void {
  #if defined(__linux__)
  if(!s.geTid) return;
  if(!s.gePin.empty()) {
    s.gePinAsked++;
    if(int error = setAffinity(s.gePin, s.geTid)) s.gePinRefused++, s.gePinError = error;
  }
  s.geFramesByCpu[taskTime("/proc/self/task/" + std::to_string(s.geTid)).lastCpu]++;
  #else
  (void)s;
  #endif
}

inline auto print() -> void;
inline auto writeJson(State& s) -> void;

inline auto begin() -> void {
  auto& s = state();
  if(s.began) return;
  s.began = true;
  s.enabled = environment("PHOBOS_MEASURE", "1") != "0";
  if(!s.enabled) return;
  s.tid = threadId();
  s.jsonFile = environment("PHOBOS_MEASURE_JSON");
  s.freqEvery = (unsigned)std::strtoul(environment("PHOBOS_FREQ_EVERY", "30").c_str(), nullptr, 10);
  s.cpuList = cpus();
  s.cpuset = readLine("/proc/self/cpuset");
  s.cgroup = readLine("/proc/self/cgroup");
  utsname name{};
  if(!uname(&name)) s.kernel = std::string(name.sysname) + " " + name.release + " " + name.machine;
  s.android = property("ro.build.version.release");
  s.model = property("ro.product.model");
  s.soc = property("ro.soc.model");
  s.harden = property("security.perf_harden");
  s.paranoid = perfParanoid();
  s.execRefused = executableMemoryRefused();
  if(s.execRefused) {
    std::fprintf(stderr, "measure: memory code may run from is refused (mmap PROT_EXEC: %s): the recompiler can't "
                         "run here, and the interpreter runs everything\n", std::strerror(s.execRefused));
  }

  //The pinning, as the app pins its emulation thread (or as asked), before anything is counted.
  s.pinMode = environment("PHOBOS_PIN", "app");
  #if defined(__linux__)
  s.affinityBefore = affinity();
  errno = 0;
  s.niceBefore = getpriority(PRIO_PROCESS, 0);
  if(s.pinMode != "none") {
    s.niceAsked = true;
    s.niceValue = std::atoi(environment("PHOBOS_NICE", "-10").c_str());
    if(setpriority(PRIO_PROCESS, 0, s.niceValue)) s.niceError = errno;  //(Linux: this thread's)
  }
  errno = 0;
  s.niceAfter = getpriority(PRIO_PROCESS, 0);
  long count = sysconf(_SC_NPROCESSORS_CONF);
  if(s.pinMode == "app") {
    for(long cpu = std::max(0L, count - 4); cpu < count; cpu++) s.wide.push_back((int)cpu);
    setAffinity(s.wide);
    s.target = fastestCpus(s.cpuList);
    if(s.target.empty()) s.target = s.wide;
  } else if(s.pinMode == "fastest") {
    s.target = fastestCpus(s.cpuList);
  } else if(s.pinMode != "none") {
    s.target = parseCpus(s.pinMode);
    if(s.target.empty()) std::fprintf(stderr, "measure: PHOBOS_PIN=%s isn't app, fastest, none or CPUs\n",
                                      s.pinMode.c_str());
  }
  pinFrame(s);  //(the app asks before each frame's run, the first one's too)
  //(the GE's own thread, there from the game's load if "GE Thread" is On: looked for once)
  s.gePin = parseCpus(environment("PHOBOS_GE_PIN", ""));
  s.geTid = geThreadId();
  if(s.geTid) s.geAffinityBegin = affinityOf(s.geTid);
  if(s.geTid && !s.gePin.empty()) s.gePinAsked++, setAffinity(s.gePin, s.geTid);
  #endif

  s.clocksBegin = policyClocks(s.cpuList);
  s.threadsBegin = threads();
  s.frameMs.reserve(4096);
  std::atexit([] { print(); });
  s.taskBegin = taskTime("/proc/self/task/" + std::to_string(s.tid));
  #if defined(__linux__)
  s.threadUsage = getrusage(RUSAGE_THREAD, &s.usageBegin) == 0;
  #endif
  if(!s.threadUsage) getrusage(RUSAGE_SELF, &s.usageBegin);
  //(the counters last: they count from here)
  if(environment("PHOBOS_PERF", "1") != "0") perfStart(s.perf);
  else s.perf.error = "not tried (PHOBOS_PERF=0)";
  s.processBegin = clockSeconds(CLOCK_PROCESS_CPUTIME_ID);
  s.threadBegin = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  s.wallBegin = s.lastFrame = steadySeconds();
}

inline auto frame() -> void {
  auto& s = state();
  if(!s.enabled || !s.began || s.ended) return;
  double now = steadySeconds();
  s.frameMs.push_back(float((now - s.lastFrame) * 1000));
  s.lastFrame = now;
  s.frames++;
  int cpu = currentCpu();
  s.framesByCpu[cpu]++;
  if(s.freqEvery && s.frames % s.freqEvery == 0 && cpu >= 0) {
    long long khz = 0;
    if(readNumber("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_cur_freq", khz)) {
      s.samples.push_back({s.frames, cpu, khz});
    }
  }
  pinFrame(s);
  geFrame(s);
}

inline auto end() -> void {
  auto& s = state();
  if(!s.enabled || !s.began || s.ended) return;
  s.wallEnd = steadySeconds();
  s.threadEnd = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  s.processEnd = clockSeconds(CLOCK_PROCESS_CPUTIME_ID);
  perfStop(s.perf);
  #if defined(__linux__)
  if(s.threadUsage) getrusage(RUSAGE_THREAD, &s.usageEnd);
  else   getrusage(RUSAGE_SELF, &s.usageEnd);
  s.affinityEnd = affinity();
  if(s.geTid) s.geAffinityEnd = affinityOf(s.geTid);
  #else
  getrusage(RUSAGE_SELF, &s.usageEnd);
  #endif
  s.taskEnd = taskTime("/proc/self/task/" + std::to_string(s.tid));
  s.threadsEnd = threads();
  s.clocksEnd = policyClocks(s.cpuList);
  s.ended = true;
  writeJson(s);
}

//What came of it, worked out for the lines and the JSON alike.
struct Results {
  double window = 0, fps = 0, threadCpu = 0, processCpu = 0, user = 0, system = 0, waiting = -1;
  u64 slices = 0, voluntary = 0, involuntary = 0, minorFaults = 0, majorFaults = 0;
  std::map<std::string, double> totals;  //the counters' totals across the PMUs, by name
  std::map<std::string, double> counted; //an extra event's share of the time it could count that it had a counter
  std::vector<std::string> uncounted;    //the extra events that never had one (no total: not a zero)
  struct Busy { long tid; std::string name; double cpu; };
  std::vector<Busy> busy;                //the threads' CPU time in the window, the most first
  double meanMs = 0, medianMs = 0, p90Ms = 0, p99Ms = 0, maxMs = 0;
  unsigned maxFrame = 0;
  struct Clock { unsigned samples = 0; long long low = 0, high = 0; double mean = 0; };
  std::map<int, Clock> clocks;
};

inline auto seconds(const timeval& time) -> double { return time.tv_sec + time.tv_usec * 1e-6; }

inline auto results(const State& s) -> Results {
  Results r;
  r.window = s.wallEnd - s.wallBegin;
  r.fps = r.window > 0 ? s.frames / r.window : 0;
  r.threadCpu = s.threadBegin >= 0 && s.threadEnd >= 0 ? s.threadEnd - s.threadBegin : -1;
  r.processCpu = s.processBegin >= 0 && s.processEnd >= 0 ? s.processEnd - s.processBegin : -1;
  r.user = seconds(s.usageEnd.ru_utime) - seconds(s.usageBegin.ru_utime);
  r.system = seconds(s.usageEnd.ru_stime) - seconds(s.usageBegin.ru_stime);
  r.voluntary = s.usageEnd.ru_nvcsw - s.usageBegin.ru_nvcsw;
  r.involuntary = s.usageEnd.ru_nivcsw - s.usageBegin.ru_nivcsw;
  r.minorFaults = s.usageEnd.ru_minflt - s.usageBegin.ru_minflt;
  r.majorFaults = s.usageEnd.ru_majflt - s.usageBegin.ru_majflt;
  if(s.taskBegin.schedstat && s.taskEnd.schedstat) {
    r.waiting = (s.taskEnd.wait - s.taskBegin.wait) * 1e-9;
    r.slices = s.taskEnd.slices - s.taskBegin.slices;
  }
  //The counters' totals. A core group is pinned, never sharing a counter, so its counts stand as they are (on a PMU of
  //some cores alone, they count while the thread is on those, and the PMUs' counts add up). An extra event may have
  //had a counter for only some of that time, taking turns: its count is taken to the whole of its PMU's core group's
  //time (else its own time enabled), a share kept in counted.
  std::map<std::string, u64> onPmu;  //(each core group's time counting: the thread's time on its PMU's cores)
  for(auto& group : s.perf.groups) if(group.kind == "core" && group.read) onPmu[group.pmu] = group.running;
  std::vector<std::string> extras;
  std::map<std::string, std::pair<u64, u64>> turns;  //an extra event's time with a counter, and the time it could count
  for(auto& group : s.perf.groups) {
    for(auto& counter : group.counters) {
      if(group.kind != "extra") {
        if(group.read) r.totals[counter.name] += (double)counter.value;
        continue;
      }
      if(!turns.count(counter.name)) extras.push_back(counter.name);
      auto& turn = turns[counter.name];
      if(!group.read) continue;
      u64 whole = onPmu.count(group.pmu) ? onPmu[group.pmu] : group.enabled;
      turn.first += group.running, turn.second += whole;
      if(group.running) r.totals[counter.name] += (double)counter.value * (double)whole / (double)group.running;
    }
  }
  for(auto& name : extras) {
    auto& turn = turns[name];
    if(turn.first) r.counted[name] = std::min(1.0, (double)turn.first / (double)std::max<u64>(turn.second, 1));
    else r.uncounted.push_back(name);
  }
  for(auto& [tid, thread] : s.threadsEnd) {
    u64 before = 0;
    if(auto found = s.threadsBegin.find(tid); found != s.threadsBegin.end()) before = found->second.time.run;
    if(!thread.time.valid) continue;
    double cpu = (thread.time.run - std::min(before, thread.time.run)) * 1e-9;
    r.busy.push_back({tid, thread.name, cpu});
  }
  std::sort(r.busy.begin(), r.busy.end(), [](auto& a, auto& b) { return a.cpu > b.cpu; });
  if(!s.frameMs.empty()) {
    auto sorted = s.frameMs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    for(float ms : s.frameMs) sum += ms;
    r.meanMs = sum / s.frameMs.size();
    auto at = [&](double fraction) { return sorted[std::min(sorted.size() - 1, size_t(fraction * sorted.size()))]; };
    r.medianMs = at(0.5), r.p90Ms = at(0.9), r.p99Ms = at(0.99), r.maxMs = sorted.back();
    r.maxFrame = unsigned(std::max_element(s.frameMs.begin(), s.frameMs.end()) - s.frameMs.begin()) + 1;
  }
  for(auto& sample : s.samples) {
    auto& clock = r.clocks[sample.cpu];
    if(!clock.samples || sample.khz < clock.low) clock.low = sample.khz;
    if(!clock.samples || sample.khz > clock.high) clock.high = sample.khz;
    clock.mean += sample.khz;
    clock.samples++;
  }
  for(auto& [cpu, clock] : r.clocks) clock.mean /= clock.samples;
  return r;
}

inline auto total(const Results& r, const char* name) -> double {
  auto found = r.totals.find(name);
  return found == r.totals.end() ? -1 : found->second;
}

inline auto kindOf(const State& s, int cpu) -> std::string {
  for(auto& entry : s.cpuList) if(entry.id == cpu) return entry.kind;
  return {};
}

inline auto print() -> void {
  auto& s = state();
  if(!s.enabled || !s.ended || s.printed) return;
  s.printed = true;
  auto r = results(s);
  if(s.execRefused) {
    std::printf("measure warning: memory code may run from was refused (mmap PROT_EXEC: %s; SELinux's execmem): the "
                "recompiler fell back to the interpreter, so this run isn't the app's\n", std::strerror(s.execRefused));
  }
  std::printf("measure window: %u frames in %.3f s, %.2f a second (the frame loop, as the measurements timed it)\n",
              s.frames, r.window, r.fps);
  std::printf("measure emulation thread: tid %ld, %.3f s of CPU (%.1f%% of the window", s.tid, r.threadCpu,
              r.window > 0 ? 100 * r.threadCpu / r.window : 0.0);
  if(s.threadUsage) std::printf("; %.3f user, %.3f system", r.user, r.system);
  std::printf(")");
  if(r.waiting >= 0) std::printf(", %.3f s waiting for a CPU in %llu timeslices", r.waiting, r.slices);
  std::printf(", %llu voluntary and %llu involuntary switches, %llu minor and %llu major faults%s\n", r.voluntary,
              r.involuntary, r.minorFaults, r.majorFaults,
              s.threadUsage ? "" : " (the process's: no per-thread usage here)");
  std::string where;
  for(auto& [cpu, frames] : s.framesByCpu) {
    if(cpu < 0) continue;
    char item[96];
    auto kind = kindOf(s, cpu);
    std::snprintf(item, sizeof(item), "%scpu%d%s%s %u (%.1f%%)", where.empty() ? "" : ", ", cpu,
                  kind.empty() ? "" : " ", kind.c_str(), frames, s.frames ? 100.0 * frames / s.frames : 0.0);
    where += item;
  }
  if(!where.empty()) {
    std::printf("measure cpus: %s (frames by the CPU the emulation thread was on after each)\n", where.c_str());
  }
  if(!s.affinityBefore.empty()) {
    std::printf("measure pinning: %s", s.pinMode.c_str());
    if(s.niceAsked) {
      std::printf(", nice %d%s", s.niceAfter, s.niceError ? (std::string(" (asked ") + std::to_string(s.niceValue) +
                                                             ": " + std::strerror(s.niceError) + ")").c_str() : "");
    }
    if(!s.wide.empty()) std::printf(", the last four cores %s", cpuList(s.wide).c_str());
    if(!s.target.empty()) {
      std::printf(", then %s every frame (%llu asked, %llu refused%s%s)", cpuList(s.target).c_str(), s.pinAsked,
                  s.pinRefused, s.pinRefused ? ": " : "", s.pinRefused ? std::strerror(s.pinError) : "");
    }
    std::printf("; kept to %s at the end (%s before)\n", cpuList(s.affinityEnd).c_str(),
                cpuList(s.affinityBefore).c_str());
  }
  if(s.geTid) {
    std::string where;
    for(auto& [cpu, frames] : s.geFramesByCpu) {
      char item[96];
      auto kind = cpu < 0 ? std::string() : kindOf(s, cpu);
      std::snprintf(item, sizeof(item), "%scpu%d%s%s %u (%.1f%%)", where.empty() ? "" : ", ", cpu,
                    kind.empty() ? "" : " ", kind.c_str(), frames, s.frames ? 100.0 * frames / s.frames : 0.0);
      where += item;
    }
    std::printf("measure ge thread: tid %ld, %s (frames by the CPU it was on after each)", s.geTid, where.c_str());
    if(!s.gePin.empty()) {
      std::printf(", pinned to %s every frame (%llu asked, %llu refused%s%s)", cpuList(s.gePin).c_str(), s.gePinAsked,
                  s.gePinRefused, s.gePinRefused ? ": " : "", s.gePinRefused ? std::strerror(s.gePinError) : "");
    }
    std::printf("; kept to %s at the end (%s at the start)\n", cpuList(s.geAffinityEnd).c_str(),
                cpuList(s.geAffinityBegin).c_str());
  }
  if(!r.clocks.empty()) {
    std::string clocks;
    for(auto& [cpu, clock] : r.clocks) {
      char item[128];
      std::snprintf(item, sizeof(item), "%scpu%d %.0f MHz (%u samples, %lld to %lld)", clocks.empty() ? "" : ", ",
                    cpu, clock.mean / 1000, clock.samples, clock.low / 1000, clock.high / 1000);
      clocks += item;
    }
    std::printf("measure clocks: %s (scaling_cur_freq of its CPU every %u frames)\n", clocks.c_str(), s.freqEvery);
  }
  double instructions = total(r, "instructions"), cycles = total(r, "cycles");
  if(s.perf.available && instructions >= 0 && cycles > 0) {
    std::printf("measure counters: %.6g instructions, %.6g cycles (IPC %.3f, %.3f GHz over its CPU time)",
                instructions, cycles, instructions / cycles, r.threadCpu > 0 ? cycles / r.threadCpu / 1e9 : 0.0);
    double frontend = total(r, "frontend stall cycles"), backend = total(r, "backend stall cycles");
    double branches = total(r, "mispredicted branches"), l1i = total(r, "L1I refills"), l1d = total(r, "L1D refills");
    if(frontend >= 0) std::printf(", frontend stalls %.1f%%", 100 * frontend / cycles);
    if(backend >= 0) std::printf(", backend stalls %.1f%%", 100 * backend / cycles);
    if(branches >= 0 && instructions > 0) std::printf(", %.2f mispredicts", 1000 * branches / instructions);
    if(l1i >= 0 && instructions > 0) std::printf(", %.2f L1I refills", 1000 * l1i / instructions);
    if(l1d >= 0 && instructions > 0) std::printf(", %.2f L1D refills", 1000 * l1d / instructions);
    if(branches >= 0 || l1i >= 0 || l1d >= 0) std::printf(" per 1000 instructions");
    double least = 1, most = 0;  //(the extra events' turns with a counter, where they took turns)
    for(auto& [name, share] : r.counted) least = std::min(least, share), most = std::max(most, share);
    if(!r.counted.empty() && least < 0.995) {
      if(std::lround(100 * least) == std::lround(100 * most)) {
        std::printf(" (counted %.0f%% of the time, scaled)", 100 * least);
      } else {
        std::printf(" (counted %.0f%% to %.0f%% of the time, scaled)", 100 * least, 100 * most);
      }
    }
    double migrations = total(r, "migrations");
    if(migrations >= 0) std::printf(", %.0f migrations", migrations);
    std::printf("; user mode");
    unsigned pmus = 0;
    for(auto& group : s.perf.groups) pmus += group.kind == "core";
    for(auto& group : s.perf.groups) {
      if(group.kind != "core") continue;
      double groupInstructions = 0, groupCycles = 0;
      for(auto& counter : group.counters) {
        if(counter.name == "instructions") groupInstructions = (double)counter.value;
        if(counter.name == "cycles") groupCycles = (double)counter.value;
      }
      std::printf(", %s (cpus %s)", group.pmu.c_str(), group.cpus.empty() ? "any" : group.cpus.c_str());
      if(pmus > 1) std::printf(": %.4g instructions, %.4g cycles", groupInstructions, groupCycles);
      if(group.read && group.running < group.enabled) {
        std::printf(" (on its cores %.1f%% of the time)", 100.0 * group.running / std::max<u64>(group.enabled, 1));
      }
    }
    if(!r.uncounted.empty()) {
      std::string names;
      for(auto& name : r.uncounted) names += (names.empty() ? "" : ", ") + name;
      std::printf("; not counted: %s (never given a counter: the PMU's others were all in use)", names.c_str());
    }
    std::printf("\n");
  } else {
    std::printf("measure counters: none (%s)\n", s.perf.error.empty() ? "unknown" : s.perf.error.c_str());
  }
  if(!s.perf.denied) {
    for(auto& problem : s.perf.problems) std::printf("measure counter problem: %s\n", problem.c_str());
  }
  if(!s.frameMs.empty()) {
    std::printf("measure frame times: mean %.2f ms, median %.2f, 90%% %.2f, 99%% %.2f, longest %.2f (frame %u)\n",
                r.meanMs, r.medianMs, r.p90Ms, r.p99Ms, r.maxMs, r.maxFrame);
  }
  if(!r.busy.empty()) {
    std::string list;
    unsigned shown = 0;
    double rest = 0;
    for(auto& thread : r.busy) {
      if(shown < 8 && thread.cpu >= 0.0005) {
        char item[128];
        std::snprintf(item, sizeof(item), "%s%s %ld%s %.3f", list.empty() ? "" : ", ", thread.name.c_str(),
                      thread.tid, thread.tid == s.tid ? " (emulation)" : "", thread.cpu);
        list += item;
        shown++;
      } else rest += thread.cpu;
    }
    std::printf("measure threads: %.3f s of CPU in the window, %zu threads: %s%s\n", r.processCpu, r.busy.size(),
                list.c_str(), rest >= 0.0005 ? (", the rest " + jsonNumber(rest, "%.3f")).c_str() : "");
  }
  if(!s.jsonFile.empty()) std::printf("measure json: %s\n", s.jsonFile.c_str());
  std::fflush(stdout);
}

inline auto writeJson(State& s) -> void {
  if(s.jsonFile.empty()) return;
  auto r = results(s);
  auto number = [](double value) { return jsonNumber(value); };
  auto list = [](const std::vector<int>& cpus) {
    std::string text = "[";
    for(size_t n = 0; n < cpus.size(); n++) text += (n ? ", " : "") + std::to_string(cpus[n]);
    return text + "]";
  };
  std::string out = "{\n  \"measure\": \"phobos-psp-runner-measure 1\",\n";
  out += "  \"build\": {\"source\": " + jsonText(PHOBOS_MEASURE_SOURCE) + ", \"build\": " +
         jsonText(PHOBOS_MEASURE_BUILD) + "},\n";
  out += "  \"system\": {\"kernel\": " + jsonText(s.kernel) + ", \"android\": " + jsonText(s.android) +
         ", \"model\": " + jsonText(s.model) + ", \"soc\": " + jsonText(s.soc) + ", \"perf_event_paranoid\": " +
         (s.paranoid == -1000 ? std::string("null") : std::to_string(s.paranoid)) + ", \"security.perf_harden\": " +
         jsonText(s.harden) + ", \"executable memory\": " + (s.execRefused ? "false" : "true") +
         ", \"executable memory error\": " + (s.execRefused ? jsonText(std::strerror(s.execRefused)) : "null") +
         ", \"cpuset\": " + jsonText(s.cpuset) + ", \"cgroup\": " + jsonText(s.cgroup) + ", \"cpus\": [";
  for(size_t n = 0; n < s.cpuList.size(); n++) {
    auto& cpu = s.cpuList[n];
    out += std::string(n ? ", " : "") + "{\"cpu\": " + std::to_string(cpu.id) + ", \"kind\": " + jsonText(cpu.kind) +
           ", \"capacity\": " + std::to_string(cpu.capacity) + ", \"max khz\": " + std::to_string(cpu.maxKHz) +
           ", \"online\": " + (cpu.online ? "true" : "false") + "}";
  }
  out += "]},\n";
  out += "  \"pin\": {\"mode\": " + jsonText(s.pinMode) + ", \"nice\": " + std::to_string(s.niceAfter) +
         ", \"nice before\": " + std::to_string(s.niceBefore) + ", \"nice error\": " +
         (s.niceError ? jsonText(std::strerror(s.niceError)) : std::string("null")) + ", \"wide\": " + list(s.wide) +
         ", \"target\": " + list(s.target) + ", \"asked\": " + std::to_string(s.pinAsked) + ", \"refused\": " +
         std::to_string(s.pinRefused) + ", \"affinity before\": " + list(s.affinityBefore) +
         ", \"affinity at end\": " + list(s.affinityEnd) + "},\n";
  out += "  \"ge thread\": ";
  if(!s.geTid) {
    out += "null,\n";
  } else {
    out += "{\"tid\": " + std::to_string(s.geTid) + ", \"pin\": " + list(s.gePin) + ", \"asked\": " +
           std::to_string(s.gePinAsked) + ", \"refused\": " + std::to_string(s.gePinRefused) +
           ", \"affinity at start\": " + list(s.geAffinityBegin) + ", \"affinity at end\": " + list(s.geAffinityEnd) +
           ", \"frames by cpu\": {";
    bool firstCpu = true;
    for(auto& [cpu, frames] : s.geFramesByCpu) {
      out += std::string(firstCpu ? "" : ", ") + "\"" + std::to_string(cpu) + "\": " + std::to_string(frames);
      firstCpu = false;
    }
    out += "}},\n";
  }
  out += "  \"window\": {\"frames\": " + std::to_string(s.frames) + ", \"seconds\": " + number(r.window) +
         ", \"frames a second\": " + number(r.fps) + "},\n";
  out += "  \"emulation thread\": {\"tid\": " + std::to_string(s.tid) + ", \"cpu seconds\": " + number(r.threadCpu) +
         ", \"user seconds\": " + number(r.user) + ", \"system seconds\": " + number(r.system) +
         ", \"waiting seconds\": " + (r.waiting >= 0 ? number(r.waiting) : std::string("null")) +
         ", \"timeslices\": " + (r.waiting >= 0 ? std::to_string(r.slices) : std::string("null")) +
         ", \"voluntary switches\": " + std::to_string(r.voluntary) + ", \"involuntary switches\": " +
         std::to_string(r.involuntary) + ", \"minor faults\": " + std::to_string(r.minorFaults) +
         ", \"major faults\": " + std::to_string(r.majorFaults) + ", \"rusage\": " +
         (s.threadUsage ? "\"thread\"" : "\"process\"") + "},\n";
  out += "  \"frames by cpu\": {";
  bool first = true;
  for(auto& [cpu, frames] : s.framesByCpu) {
    out += std::string(first ? "" : ", ") + "\"" + std::to_string(cpu) + "\": " + std::to_string(frames);
    first = false;
  }
  out += "},\n  \"clocks\": {\"every\": " + std::to_string(s.freqEvery) + ", \"by cpu\": {";
  first = true;
  for(auto& [cpu, clock] : r.clocks) {
    out += std::string(first ? "" : ", ") + "\"" + std::to_string(cpu) + "\": {\"samples\": " +
           std::to_string(clock.samples) + ", \"low khz\": " + std::to_string(clock.low) + ", \"mean khz\": " +
           number(clock.mean) + ", \"high khz\": " + std::to_string(clock.high) + "}";
    first = false;
  }
  out += "}, \"samples\": [";
  for(size_t n = 0; n < s.samples.size(); n++) {
    out += std::string(n ? ", " : "") + "[" + std::to_string(s.samples[n].frame) + ", " +
           std::to_string(s.samples[n].cpu) + ", " + std::to_string(s.samples[n].khz) + "]";
  }
  auto clocks = [](const std::map<int, long long>& policies) {
    std::string text = "{";
    for(auto& [cpu, khz] : policies) text += (text.size() > 1 ? ", " : "") + ("\"" + std::to_string(cpu)) + "\": " +
                                             std::to_string(khz);
    return text + "}";
  };
  out += "], \"policies at begin\": " + clocks(s.clocksBegin) + ", \"policies at end\": " + clocks(s.clocksEnd) +
         "},\n";
  out += "  \"counters\": {\"available\": " + std::string(s.perf.available ? "true" : "false") + ", \"error\": " +
         (s.perf.error.empty() ? std::string("null") : jsonText(s.perf.error)) + ", \"problems\": [";
  for(size_t n = 0; n < s.perf.problems.size(); n++) out += (n ? ", " : "") + jsonText(s.perf.problems[n]);
  out += "], \"mode\": \"user\", \"total\": {";
  first = true;
  for(auto& [name, value] : r.totals) {
    out += std::string(first ? "" : ", ") + jsonText(name) + ": " + jsonNumber(value, "%.0f");
    first = false;
  }
  for(auto& name : r.uncounted) {
    out += std::string(first ? "" : ", ") + jsonText(name) + ": null";
    first = false;
  }
  out += "}, \"counted\": {";
  first = true;
  for(auto& [name, share] : r.counted) {
    out += std::string(first ? "" : ", ") + jsonText(name) + ": " + jsonNumber(share);
    first = false;
  }
  out += "}, \"not counted\": [";
  for(size_t n = 0; n < r.uncounted.size(); n++) out += (n ? ", " : "") + jsonText(r.uncounted[n]);
  out += "], \"groups\": [";
  for(size_t n = 0; n < s.perf.groups.size(); n++) {
    auto& group = s.perf.groups[n];
    out += std::string(n ? ", " : "") + "{\"pmu\": " + jsonText(group.pmu) + ", \"cpus\": " + jsonText(group.cpus) +
           ", \"kind\": " + jsonText(group.kind) + ", \"enabled ns\": " + std::to_string(group.enabled) +
           ", \"running ns\": " + std::to_string(group.running) + ", \"values\": {";
    for(size_t m = 0; m < group.counters.size(); m++) {
      out += (m ? ", " : "") + jsonText(group.counters[m].name) + ": " + std::to_string(group.counters[m].value);
    }
    out += "}}";
  }
  out += "]},\n";
  out += "  \"frame times\": {\"mean ms\": " + number(r.meanMs) + ", \"median ms\": " + number(r.medianMs) +
         ", \"p90 ms\": " + number(r.p90Ms) + ", \"p99 ms\": " + number(r.p99Ms) + ", \"longest ms\": " +
         number(r.maxMs) + ", \"longest frame\": " + std::to_string(r.maxFrame) + "},\n";
  out += "  \"threads\": {\"process cpu seconds\": " + number(r.processCpu) + ", \"list\": [";
  for(size_t n = 0; n < r.busy.size(); n++) {
    auto& thread = r.busy[n];
    out += std::string(n ? ", " : "") + "{\"tid\": " + std::to_string(thread.tid) + ", \"name\": " +
           jsonText(thread.name) + ", \"cpu seconds\": " + number(thread.cpu) +
           (thread.tid == s.tid ? ", \"emulation\": true" : "") + "}";
  }
  out += "]}\n}\n";
  std::string part = s.jsonFile + ".part-" + std::to_string(getpid());
  FILE* file = std::fopen(part.c_str(), "w");
  bool written = file && std::fwrite(out.data(), 1, out.size(), file) == out.size();
  if(file) written = std::fclose(file) == 0 && written;
  if(!written || std::rename(part.c_str(), s.jsonFile.c_str())) {
    std::remove(part.c_str());
    std::fprintf(stderr, "measure: the JSON can't be written: %s\n", s.jsonFile.c_str());
  }
}

}
