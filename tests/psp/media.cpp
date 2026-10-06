//The stubs and odds and ends of docs/psp-core.md's part 20: sceMpeg setting a movie up and finding nothing to play
//(mpeg.cpp), sceAtrac3plus refusing every stream (atrac.cpp), the network libraries with the wireless LAN off
//(net.cpp), and the small functions games asked for: the local time, the OpenPSID, the display's line count and
//rate, the CPU's interrupts and the kernel's memset and memcpy, later SDKs' clock setter, the AV modules, the
//thread priority functions, holding off dispatch, and the lightweight mutex's CB lock. Programs run on both engines.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Buffer = 0x0893'0000;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }
}

//sceMpeg: the sizes video/mpeg's tests recorded, a ringbuffer filled in as ringbuffer/construct recorded (and its
//refusals), the library's signature, then a header that can't be read and streams with nothing in them.
static auto mpegStubs() -> void {
  KernelMachine m;
  for(auto [packets, size] : {std::pair{0u, 0u}, {1u, 0x868u}, {0x200u, 0x10'd000u}, {0x1000u, 0x86'8000u},
                              {u32(-1), 0xffff'f798u}, {0x8000'0000u, 0u}}) {
    check(__LINE__, "a ringbuffer's memory", m.call("sceMpegRingbufferQueryMemSize", {packets}), size);
  }
  CHECK(m.call("sceMpegInit", {}), 0);
  CHECK(m.call("sceMpegQueryMemSize", {0}), 0x10000);
  constexpr u32 Ring = R + 0x100, Data = 0x0894'0000;
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 512, Data, 0x10'd000, 0x0880'4000, 0xdead'beef}), 0);
  CHECK(word(m, Ring) == 512 && word(m, Ring + 4) == 0 && word(m, Ring + 8) == 0 && word(m, Ring + 12) == 0, true);
  CHECK(word(m, Ring + 20) == Data && word(m, Ring + 24) == 0x0880'4000 && word(m, Ring + 28) == 0xdead'beef, true);
  CHECK(word(m, Ring + 32), Data + 512 * 2048);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 4097, Data, 0x1000, 0, 0}), 0x8061'0022);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 1, Data, u32(-1), 0, 0}), 0x8061'0022);
  CHECK(m.call("sceMpegRingbufferConstruct", {Ring, 512, Data, 0x10'd000, 0x0880'4000, 0}), 0);
  CHECK(m.call("sceMpegRingbufferAvailableSize", {Ring}), 512);
  constexpr u32 Handle = R + 0x40, Memory = 0x0895'0000;
  CHECK(m.call("sceMpegCreate", {Handle, Memory, 0x10000, Ring, 512, 0, 0}), 0);
  CHECK(m.system.memory.readString(word(m, Handle), 8) == "LIBMPEG", true);
  CHECK(m.call("sceMpegCreate", {Handle, Memory, 0xffff, Ring, 512, 0, 0}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceMpegQueryStreamOffset", {Handle, Buffer, R}), 0x8061'0022);
  CHECK(m.call("sceMpegQueryStreamSize", {Buffer, R}), 0x8061'0022);
  u32 stream = m.call("sceMpegRegistStream", {Handle, 0, 0});
  CHECK(stream != 0 && stream < 0x8000'0000, true);
  CHECK(m.call("sceMpegMallocAvcEsBuf", {Handle}), 1);
  CHECK(m.call("sceMpegRingbufferPut", {Ring, 0x80, 512}), 0);
  CHECK(m.call("sceMpegGetAvcAu", {Handle, stream, R, R + 4}), 0x8061'8001);
  CHECK(m.call("sceMpegGetAtracAu", {Handle, stream, R, R + 4}), 0x8061'8001);
  CHECK(m.call("sceMpegAtracDecode", {Handle, R, Buffer, 0}), 0x807f'00fd);
  CHECK(m.call("sceMpegQueryAtracEsSize", {Handle, R, R + 4}), 0);
  CHECK(word(m, R) == 0x840 && word(m, R + 4) == 0x2000, true);
  m.system.memory.write(4, R + 8, 1);
  CHECK(m.call("sceMpegAvcDecode", {Handle, R, 512, Buffer, R + 8}), 0);
  CHECK(word(m, R + 8), 0);  //no frame
  CHECK(m.call("sceMpegAvcQueryYCbCrSize", {Handle, 1, 480, 272, R + 12}), 0);
  CHECK(word(m, R + 12) >= 480 * 272 * 3 / 2, true);
  CHECK(m.call("sceMpegAvcQueryYCbCrSize", {Handle, 1, 0x2000, 272, R + 12}), 0x8061'0022);
  for(auto name : {"sceMpegAvcInitYCbCr", "sceMpegAvcDecodeFlush", "sceMpegFlushAllStream", "sceMpegFreeAvcEsBuf",
                   "sceMpegUnRegistStream", "sceMpegDelete", "sceMpegRingbufferDestruct", "sceMpegFinish"}) {
    check(__LINE__, name, m.call(name, {Handle, 1, 480, 272, Buffer}), 0);
  }
  CHECK(m.notes.size(), 0);
}

//sceAtrac3plus: its six IDs handed out and given back; no stream taken, on an ID or with one; the rest refuse their
//ID.
static auto atracStubs() -> void {
  KernelMachine m;
  for(u32 id = 0; id < 6; id++) check(__LINE__, "an ID", m.call("sceAtracGetAtracID", {0x1000}), id);
  CHECK(m.call("sceAtracGetAtracID", {0x1001}), 0x8063'0007);
  CHECK(m.call("sceAtracGetAtracID", {0x1002}), Kernel::ErrorInvalidValue);
  CHECK(m.call("sceAtracReleaseAtracID", {3}), 0);
  CHECK(m.call("sceAtracReleaseAtracID", {3}), 0x8063'0005);
  CHECK(m.call("sceAtracReleaseAtracID", {6}), 0x8063'0005);
  CHECK(m.call("sceAtracGetAtracID", {0x1001}), 3);
  CHECK(m.call("sceAtracSetDataAndGetID", {Buffer, 0x1000}), 0x8063'0006);
  CHECK(m.call("sceAtracSetData", {3, Buffer, 0x1000}), 0x8063'0006);
  m.call("sceAtracReleaseAtracID", {3});
  CHECK(m.call("sceAtracSetData", {3, Buffer, 0x1000}), 0x8063'0005);
  CHECK(m.call("sceAtracDecodeData", {0, Buffer, R, R + 4, R + 8}), 0x8063'0005);
  CHECK(m.call("sceAtracGetRemainFrame", {0, R}), 0x8063'0005);
  CHECK(m.kernel.atracIDs, 0x37);
  CHECK(m.notes.size(), 0);
}

//The network libraries with the switch off: they start and stop, nothing connects, lists are empty, the PSP's own
//address and its text.
static auto networkOff() -> void {
  KernelMachine m;
  CHECK(m.call("sceWlanGetSwitchState", {}), 0);
  for(auto name : {"sceNetInit", "sceNetAdhocInit", "sceNetAdhocctlInit", "sceNetAdhocMatchingInit",
                   "sceNetInetInit", "sceNetApctlInit", "sceNetResolverInit", "sceNetAdhocctlAddHandler",
                   "sceNetAdhocctlDisconnect", "sceNetAdhocMatchingTerm", "sceNetAdhocctlTerm", "sceNetAdhocTerm",
                   "sceNetTerm"}) {
    check(__LINE__, name, m.call(name, {0x2000, 0x30, 0x1000, 0x30, 0x1000}), 0);
  }
  for(auto name : {"sceNetAdhocctlConnect", "sceNetAdhocctlCreate", "sceNetAdhocctlScan", "sceNetAdhocPdpCreate",
                   "sceNetAdhocPtpOpen", "sceNetAdhocMatchingCreate", "sceNetAdhocMatchingStart",
                   "sceNetApctlConnect", "sceNetResolverCreate"}) {
    check(__LINE__, name, m.call(name, {Buffer, 0, 0, 0}), Kernel::ErrorNotSupported);
  }
  m.system.memory.write(4, R, 0x1234);
  CHECK(m.call("sceNetAdhocctlGetState", {R}), 0);
  CHECK(word(m, R), 0);
  m.system.memory.write(4, R, 0x1234);
  CHECK(m.call("sceNetAdhocctlGetScanInfo", {R, Buffer}), 0);
  CHECK(word(m, R), 0);
  CHECK(m.call("sceNetGetLocalEtherAddr", {R}), 0);
  CHECK(m.call("sceNetEtherNtostr", {R, R + 0x10}), 0);
  CHECK(m.system.memory.readString(R + 0x10, 32) == "02:00:00:50:53:50", true);
  m.system.memory.copyIn(R + 0x40, "1a:2B:3c:4D:5e:6F", 18);
  CHECK(m.call("sceNetEtherStrton", {R + 0x40, R + 0x60}), 0);
  CHECK(word(m, R + 0x60) == 0x4d3c'2b1a && m.system.memory.read(2, R + 0x64) == 0x6f5e, true);
  CHECK(m.call("sceNetInetSocket", {2, 1, 0}), 0xffff'ffff);
  CHECK(m.notes.size(), 0);
}

//The small functions: the date now (UTC, in a time zone, the host's local time), a date as a time_t, the OpenPSID,
//the display's lines since power on and its rate, interrupts let through, memset and memcpy (overlapping), later
//SDKs' clock setter, the AV modules, the priority functions' refusals.
static auto oddsAndEnds() -> void {
  KernelMachine m;
  m.kernel.startTime = 1'791'290'096'000'000ull;  //2026-10-06 12:34:56 UTC
  m.kernel.cycles = 2'500'000 * (Kernel::CPUFrequency / 1'000'000);  //2.5 s on
  m.kernel.nextVblank = m.kernel.cycles + 1000;
  auto date = [&](u32 at) {
    std::array<u32, 7> fields;
    for(u32 n = 0; n < 6; n++) fields[n] = m.system.memory.read(2, at + n * 2);
    fields[6] = word(m, at + 12);
    return fields;
  };
  CHECK(m.call("sceRtcGetCurrentClock", {R, 0}), 0);
  CHECK((date(R) == std::array<u32, 7>{2026, 10, 6, 12, 34, 58, 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClock", {R, u32(-90)}), 0);  //90 minutes west
  CHECK((date(R) == std::array<u32, 7>{2026, 10, 6, 11, 4, 58, 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClockLocalTime", {R}), 0);
  std::time_t seconds = 1'791'290'098;
  std::tm local{};
  localtime_r(&seconds, &local);
  CHECK(date(R) == (std::array<u32, 7>{u32(local.tm_year + 1900), u32(local.tm_mon + 1), u32(local.tm_mday),
                                       u32(local.tm_hour), u32(local.tm_min), u32(local.tm_sec), 500'000}), true);
  CHECK(m.call("sceRtcGetCurrentClockLocalTime", {0}), Kernel::ErrorInvalidPointer);
  for(auto [address, value] : {std::pair{0u, 2000u}, {2, 1}, {4, 1}, {6, 0}, {8, 0}, {10, 1}}) {
    m.system.memory.write(2, R + 0x20 + address, value);
  }
  CHECK(m.call("sceRtcGetTime_t", {R + 0x20, R + 0x40}), 0);
  CHECK(word(m, R + 0x40), 946'684'801);
  CHECK(m.call("sceOpenPSIDGetOpenPSID", {R + 0x50}), 0);
  CHECK(m.system.memory.readString(R + 0x52, 6) == "PHOBOS" && m.system.memory.read(1, R + 0x5f) == 1, true);
  m.kernel.vblanks = 3;
  m.kernel.cycles = m.kernel.nextVblank - Kernel::VblankCycles + 10 * Kernel::LineCycles + 5;
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 3 * 286 + 10);
  m.call("sceDisplayGetFramePerSec", {});
  float rate;
  u32 bits = m.system.fpu.r[0];
  memcpy(&rate, &bits, 4);
  CHECK(rate == 59.94f, true);
  CHECK(m.call("sceKernelIsCpuIntrEnable", {}), 1);
  m.call("sceKernelCpuSuspendIntr", {});
  CHECK(m.call("sceKernelIsCpuIntrEnable", {}), 0);
  m.call("sceKernelCpuResumeIntr", {1});
  for(u32 n = 0; n < 16; n++) m.system.memory.write(1, Buffer + n, n);
  CHECK(m.call("sceKernelMemcpy", {Buffer + 4, Buffer, 8}), Buffer + 4);  //overlapping, as memmove
  CHECK(word(m, Buffer + 4) == 0x0302'0100 && word(m, Buffer + 8) == 0x0706'0504, true);
  CHECK(m.call("sceKernelMemset", {Buffer, 0xaa, 3}), Buffer);
  CHECK(word(m, Buffer), 0x03aa'aaaa);
  m.system.ipu.r[4] = 333, m.system.ipu.r[5] = 333, m.system.ipu.r[6] = 166;
  m.kernel.syscall(m.kernel.importCode("scePower", 0x4699'89ad));
  CHECK(m.system.ipu.r[2], 0);
  CHECK(m.kernel.powerState.pll == 333 && m.kernel.powerState.cpu == 333 && m.kernel.powerState.bus == 166, true);
  CHECK(m.call("sceUtilityLoadAvModule", {1}), 0);
  CHECK(m.kernel.utilityModules == std::vector<u32>{0x301}, true);
  CHECK(m.call("sceUtilityLoadAvModule", {1}), 0x8011'1102);
  CHECK(m.call("sceUtilityLoadAvModule", {8}), 0x8011'1101);
  CHECK(m.call("sceUtilityUnloadAvModule", {1}), 0);
  CHECK(m.call("sceUtilityUnloadAvModule", {1}), 0x8011'1103);
  for(u32 priority : {u32(-1), 1u, 7u, 0x78u}) {
    CHECK(m.call("sceKernelRotateThreadReadyQueue", {priority}), Kernel::ErrorIllegalPriority);
  }
  for(u32 priority : {8u, 0x77u}) CHECK(m.call("sceKernelRotateThreadReadyQueue", {priority}), 0);
  CHECK(m.notes.size(), 0);
}

//Threads: a thread of the caller's priority runs when the caller rotates its line, and the caller reads its priority;
//with dispatch held off, a higher-priority thread started doesn't run, and a wait is refused, until dispatch is
//resumed, when it runs at once; a lightweight mutex locked with callbacks runs those notified first. On both
//engines.
static auto threadOddsAndEnds() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler equal{m, 0x0880'2000};
    equal.print("equal\n");
    equal.call("sceKernelExitThread");
    Assembler high{m, 0x0880'2100};
    high.print("high\n");
    high.call("sceKernelExitThread");
    Assembler callback{m, 0x0880'2200};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.print("callback\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {
      main.li(a0, m.string("thread")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    start(0x0880'2000, 0x20);  //equal: it waits its turn
    main.print("main\n");
    main.li(a0, 0);
    main.call("sceKernelRotateThreadReadyQueue");
    main.print("rotated\n");
    main.call("sceKernelGetThreadCurrentPriority");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s0, v0, zero));
    main.li(t0, R); main.put(sw(v0, 4, t0));
    start(0x0880'2100, 0x10);  //higher: held off
    main.print("held\n");
    main.li(a0, 100);
    main.call("sceKernelDelayThread");
    main.li(t0, R); main.put(sw(v0, 8, t0));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelResumeDispatchThread");
    main.print("resumed\n");
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'2200); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(a0, v0, zero)); main.li(a1, 0);
    main.call("sceKernelNotifyCallback");
    main.li(a0, R + 0x40); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, R + 0x40); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelLockLwMutexCB");
    main.li(t0, R); main.put(sw(v0, 12, t0));
    main.print("locked\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "main\nequal\nrotated\nheld\nhigh\nresumed\ncallback\nlocked\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R), 0x20);
    CHECK(word(m, R + 4), 1);
    CHECK(word(m, R + 8), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 12), 0);
    CHECK(m.kernel.dispatchSuspended, false);
    CHECK(m.notes.size(), 0);
  }
}

auto mediaTests() -> Tests {
  return {{"mpeg stubs", mpegStubs}, {"atrac stubs", atracStubs}, {"network off", networkOff},
          {"odds and ends of part 20", oddsAndEnds}, {"threads odds and ends of part 20", threadOddsAndEnds}};
}

}
