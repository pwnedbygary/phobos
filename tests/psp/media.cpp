//The stubs and odds and ends of docs/psp-core.md's part 20: sceMpeg setting a movie up and finding nothing to play
//(mpeg.cpp), sceAtrac3plus refusing every stream (atrac.cpp), the network libraries with the wireless LAN off
//(net.cpp), and the small functions games asked for: the local time, the OpenPSID, the display's line count and
//rate, the CPU's interrupts and the kernel's memset and memcpy, later SDKs' clock setter, the AV modules, the
//thread priority functions, holding off dispatch (waits refused before they change anything), and the lightweight
//mutex's CB lock (and its mutex deleted by its callback). Each group's machine, saved at its end, loads into another
//that makes the same state. Programs run on both engines.
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
  CHECK(roundTrip(m), true);
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
  CHECK(m.call("sceAtracReinit", {4, 1}), Kernel::ErrorBusy);  //IDs handed out
  for(u32 id : {0u, 1u, 2u, 4u, 5u}) m.call("sceAtracReleaseAtracID", {id});
  CHECK(m.call("sceAtracReinit", {999, 0}), Kernel::ErrorOutOfMemory);
  CHECK(m.call("sceAtracReinit", {4, 1}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
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
  CHECK(roundTrip(m), true);
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
  //DOS times, as rtc/convert recorded: 2107-09-11 24:00:00 is 4281057280; 1979 and 2108 don't fit
  for(auto [address, value] : {std::pair{0u, 2107u}, {2, 9}, {4, 11}, {6, 24}, {8, 0}, {10, 0}}) {
    m.system.memory.write(2, R + 0x20 + address, value);
  }
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0);
  CHECK(word(m, R + 0x40), 4'281'057'280u);
  m.system.memory.write(2, R + 0x20, 1979);
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0xffff'ffff);
  m.system.memory.write(2, R + 0x20, 2108);
  CHECK(m.call("sceRtcGetDosTime", {R + 0x20, R + 0x40}), 0xffff'ffff);
  for(auto [time, expected] : {std::pair{100u, std::array<u32, 7>{1980, 0, 0, 0, 3, 8, 0}},
                               {10'000'000u, std::array<u32, 7>{1980, 4, 24, 18, 52, 0, 0}}}) {
    CHECK(m.call("sceRtcSetDosTime", {R + 0x60, time}), 0);
    check(__LINE__, "a DOS time's date", date(R + 0x60) == expected, true);
  }
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
  m.system.ipu.r[4] = 222, m.system.ipu.r[5] = 222, m.system.ipu.r[6] = 111;
  m.kernel.syscall(m.kernel.importCode("scePower", 0xebd1'77d6));
  CHECK(m.system.ipu.r[2] == 0 && m.kernel.powerState.cpu == 222, true);
  CHECK(m.call("sceKernelGetGPI", {}), 0);
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
  CHECK(roundTrip(m), true);
}

//Threads: a thread of the caller's priority runs when the caller rotates its line, and the caller reads its priority;
//with dispatch held off, a higher-priority thread started doesn't run, and a wait is refused, until dispatch is
//resumed, when it runs at once; a free lightweight mutex locked with callbacks doesn't run the one notified (it
//never enters the kernel), sceKernelCheckCallback then does. On both engines.
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
    main.call("sceKernelCheckCallback");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "main\nequal\nrotated\nheld\nhigh\nresumed\nlocked\ncallback\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R), 0x20);
    CHECK(word(m, R + 4), 1);
    CHECK(word(m, R + 8), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 12), 0);
    CHECK(m.kernel.dispatchSuspended, false);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//With dispatching held off, functions that wait are refused before they change anything (CAN_NOT_WAIT), whether
//they'd have had to wait or not, as pspautotests' intr/waits recorded: a lightweight mutex another thread holds (its
//count of waiters left at 0) and a free one (left free); a receive that would take a pipe's buffer and wait for more
//(the buffer left full, no count written), a send that would hand a waiting receiver part of its message (the
//receiver left waiting for all of it); an event flag wait whose bits are set already (left set). A negative pipe size
//and a bad event flag mode are still refused as such, ahead of that. Rotating the caller's line leaves it the CPU: a
//thread of its priority runs only once the caller waits, after dispatching resumed. (The refusals came only as a
//thread would block, after the waiter count, the pipe's bytes and the flag's bits had changed; and rotating switched
//threads.) On both engines.
static auto dispatchHeldOff() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Held = R + 0x200, Free = R + 0x220, Out = 0x0894'0000;
    m.system.memory.write(4, R + 0x48, 0x1337);
    m.system.memory.write(4, R + 0x50, 0x1337);
    m.system.memory.write(4, R + 0x70, 0x1337);
    Assembler worker{m, 0x0880'2000};  //holds the first mutex
    worker.li(a0, Held); worker.li(a1, 1); worker.li(a2, 0);
    worker.call("sceKernelLockLwMutex");
    worker.call("sceKernelSleepThread");
    Assembler receiver{m, 0x0880'2100};  //waits for 0x10 bytes on the pipe without a buffer
    receiver.li(t0, R + 0x14); receiver.put(lw(a0, 0, t0)); receiver.li(a1, Out + 0x100); receiver.li(a2, 0x10);
    receiver.li(a3, 0); receiver.li(t0, R + 0x74); receiver.li(t1, 0);
    receiver.call("sceKernelReceiveMsgPipe");
    receiver.li(t0, R); receiver.put(sw(v0, 0x70, t0));
    receiver.call("sceKernelExitThread");
    Assembler other{m, 0x0880'2200};  //of main's priority
    other.print("other ran\n");
    other.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {
      main.li(a0, m.string("thread")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    auto store = [&](u32 offset) { main.li(t0, R); main.put(sw(v0, offset, t0)); };
    for(u32 work : {Held, Free}) {
      main.li(a0, work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
      main.call("sceKernelCreateLwMutex");
    }
    for(u32 size : {0x10u, 0u}) {
      main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, size); main.li(t0, 0);
      main.call("sceKernelCreateMsgPipe");
      store(size ? 0x10 : 0x14);
    }
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, Buffer); main.li(a2, 0x10); main.li(a3, 0);
    main.li(t0, 0);
    main.call("sceKernelTrySendMsgPipe");  //the buffer full
    main.li(a0, m.string("flag")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    store(0x18);
    start(0x0880'2000, 0x30);
    start(0x0880'2100, 0x30);
    main.li(a0, 1000); main.call("sceKernelDelayThread");  //the mutex held, the receiver waiting
    start(0x0880'2200, 0x20);
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s0, v0, zero));
    for(auto [work, offset] : {std::pair{Held, 0x40u}, {Free, 0x64u}}) {
      main.li(a0, work); main.li(a1, 1); main.li(a2, 0);
      main.call("sceKernelLockLwMutex");
      store(offset);
    }
    auto pipeCall = [&](const char* function, u32 pipe, u32 address, u32 size, u32 counted) {
      main.li(t0, pipe); main.put(lw(a0, 0, t0)); main.li(a1, address); main.li(a2, size); main.li(a3, 0);
      main.li(t0, counted); main.li(t1, 0);
      main.call(function);
    };
    pipeCall("sceKernelReceiveMsgPipe", R + 0x10, Out, 0x20, R + 0x48);
    store(0x44);
    pipeCall("sceKernelSendMsgPipe", R + 0x14, Buffer, 0x20, R + 0x50);
    store(0x4c);
    pipeCall("sceKernelSendMsgPipe", R + 0x14, Buffer, u32(-1), 0);
    store(0x54);
    for(auto [mode, offset] : {std::pair{0xffu, 0x58u}, {0x20u, 0x5cu}}) {  //a bad mode; AND, clearing
      main.li(t0, R + 0x18); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, mode); main.li(a3, 0);
      main.li(t0, 0);
      main.call("sceKernelWaitEventFlag");
      store(offset);
    }
    main.li(a0, 0);
    main.call("sceKernelRotateThreadReadyQueue");
    store(0x60);
    main.print("main rotated\n");
    main.put(addu(a0, s0, zero));
    main.call("sceKernelResumeDispatchThread");
    main.print("main resumed\n");
    main.li(a0, 1000); main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "main rotated\nmain resumed\nother ran\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 0x40), Kernel::ErrorCanNotWait);
    CHECK(word(m, Held) == 1 && word(m, Held + 12) == 0, true);  //held by the worker, nobody waiting
    CHECK(word(m, R + 0x64), Kernel::ErrorCanNotWait);
    CHECK(word(m, Free), 0);
    CHECK(word(m, R + 0x44) == Kernel::ErrorCanNotWait && word(m, R + 0x48) == 0x1337, true);
    CHECK(m.kernel.pipes.at(word(m, R + 0x10)).used, 0x10);
    CHECK(word(m, R + 0x4c) == Kernel::ErrorCanNotWait && word(m, R + 0x50) == 0x1337, true);
    CHECK(word(m, R + 0x70), 0x1337);  //the receiver still waits, with nothing
    bool waiting = false;
    for(auto& [uid, thread] : m.kernel.threads) {
      if(thread->wait == Kernel::Wait::PipeReceive) waiting = thread->waitDone == 0;
    }
    CHECK(waiting, true);
    CHECK(word(m, R + 0x54), Kernel::ErrorIllegalAddress);
    CHECK(word(m, R + 0x58), Kernel::ErrorIllegalMode);
    CHECK(word(m, R + 0x5c), Kernel::ErrorCanNotWait);
    CHECK(m.kernel.eventFlags.at(word(m, R + 0x18)).pattern, 1);
    CHECK(word(m, R + 0x60), 0);
    CHECK(m.kernel.dispatchSuspended, false);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A thread waiting with callbacks for a lightweight mutex another holds runs its callback, which deletes the mutex:
//back from it, the wait ends, deleted (the wait had gone on for good, as the mutex's deleting found no thread
//waiting). The holder's unlock then finds no mutex. On both engines.
static auto lwMutexDeletedInCallback() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Work = R + 0x200;
    Assembler callback{m, 0x0880'3000};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.li(a0, Work);
    callback.call("sceKernelDeleteLwMutex");
    callback.li(t0, R); callback.put(sw(v0, 0x68, t0));
    callback.print("callback deleted it\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler worker{m, 0x0880'2000};  //holds the mutex, then notifies main's callback
    worker.li(a0, Work); worker.li(a1, 1); worker.li(a2, 0);
    worker.call("sceKernelLockLwMutex");
    worker.li(a0, 2000); worker.call("sceKernelDelayThread");
    worker.li(t0, R + 0x100); worker.put(lw(a0, 0, t0)); worker.li(a1, 1);
    worker.call("sceKernelNotifyCallback");
    worker.li(a0, 2000); worker.call("sceKernelDelayThread");
    worker.li(a0, Work); worker.li(a1, 1);
    worker.call("sceKernelUnlockLwMutex");
    worker.li(t0, R); worker.put(sw(v0, 0x60, t0));
    worker.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.li(t0, R + 0x100); main.put(sw(v0, 0, t0));
    main.li(a0, Work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //it runs at once, and holds the mutex
    main.li(a0, Work); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelLockLwMutexCB");
    main.li(t0, R); main.put(sw(v0, 0x64, t0));
    main.print("main's lock returned\n");
    main.li(a0, 3000); main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "callback deleted it\nmain's lock returned\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 0x68), 0);
    CHECK(word(m, R + 0x64), Kernel::ErrorWaitDeleted);
    CHECK(word(m, R + 0x60), Kernel::ErrorLwMutexNotFound);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//sceLibFont with no fonts installed: the library starts, lists none, finds and opens none (the error written where
//asked), a font's details refused; points and pixels at 128 dots an inch, then at a resolution set. Resolutions a
//state couldn't hold (0 or less, 10^9 or more, not a number) are refused, the last one kept (sceFontSetResolution
//had kept 1e10 and the infinities, and the machine's own state was refused).
static auto fontsMissing() -> void {
  KernelMachine m;
  m.system.memory.write(4, R, 0x1337);
  u32 library = m.call("sceFontNewLib", {Buffer, R});
  CHECK(library != 0 && word(m, R) == 0, true);
  CHECK(m.call("sceFontGetNumFontList", {library, R}), 0);
  CHECK(m.call("sceFontFindOptimumFont", {library, Buffer, R}), 0xffff'ffff);
  CHECK(word(m, R), Kernel::ErrorNotFound);
  m.system.memory.write(4, R, 0);
  CHECK(m.call("sceFontOpen", {library, 0, 0, R}), 0);
  CHECK(word(m, R), Kernel::ErrorNotFound);
  CHECK(m.call("sceFontGetFontInfo", {0, Buffer}), Kernel::ErrorNotFound);
  CHECK(m.call("sceFontGetCharInfo", {0, 'A', Buffer}), Kernel::ErrorNotFound);
  auto scale = [&](const char* name, float value) {
    u32 bits;
    memcpy(&bits, &value, 4);
    m.system.fpu.r[12] = bits;
    m.call(name, {library, R});
    memcpy(&value, &m.system.fpu.r[0], 4);
    return value;
  };
  CHECK(scale("sceFontPointToPixelH", 72.0f) == 128.0f, true);
  CHECK(scale("sceFontPixelToPointV", 128.0f) == 72.0f, true);
  float resolution = 144.0f;
  memcpy(&m.system.fpu.r[12], &resolution, 4);
  memcpy(&m.system.fpu.r[13], &resolution, 4);
  CHECK(m.call("sceFontSetResolution", {library}), 0);
  CHECK(scale("sceFontPointToPixelV", 72.0f) == 144.0f, true);
  auto setResolution = [&](float horizontal, float vertical) {
    memcpy(&m.system.fpu.r[12], &horizontal, 4);
    memcpy(&m.system.fpu.r[13], &vertical, 4);
    return m.call("sceFontSetResolution", {library});
  };
  float infinity = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
  for(auto [horizontal, vertical] : {std::pair{1e10f, 144.0f}, {144.0f, infinity}, {nan, 144.0f}, {144.0f, 0.0f},
                                     {-1.0f, 144.0f}, {1e9f, 144.0f}, {144.0f, -infinity}}) {
    check(__LINE__, "a resolution refused", setResolution(horizontal, vertical), Kernel::ErrorInvalidValue);
  }
  CHECK(m.kernel.fontResolution[0] == 144.0f && m.kernel.fontResolution[1] == 144.0f, true);
  CHECK(setResolution(5e8f, 72.0f), 0);
  CHECK(scale("sceFontPointToPixelV", 72.0f) == 72.0f, true);
  CHECK(m.call("sceFontClose", {0}), 0);
  CHECK(m.call("sceFontDoneLib", {library}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//A thread starts with the kernel's 256 bytes at the top of its stack zeroed (k0 points at them), the rest of a new
//stack filled with 0xff as before.
static auto kernelArea() -> void {
  KernelMachine m;
  s32 uid = m.kernel.createThread("t", 0x0880'1000, 0x20, 0x1000, 0, 0);
  auto& thread = *m.kernel.threads[uid];
  u32 top = thread.stackBlock + thread.stackSize;
  CHECK(word(m, top - 0x100), 0xffff'ffff);
  m.kernel.startThread(thread, 0, 0);
  CHECK(thread.context.gpr[26], top - 0x100);
  bool zeroed = true;
  for(u32 at = top - 0x100; at < top; at += 4) zeroed = zeroed && word(m, at) == 0;
  CHECK(zeroed, true);
  CHECK(word(m, top - 0x104) == 0xffff'ffff && word(m, thread.stackBlock + 16) == 0xffff'ffff, true);
  CHECK(roundTrip(m), true);
}

//Dates as Win32 file times, and ticks as dates, as pspautotests' rtc/convert recorded: 100-nanosecond steps since
//1601 (2005-11-31 13:01:00 and 1 microsecond, a day past November's end, is 127779156600000010; 1601-01-01 is 0);
//dates before 1601, a zeroed one among them, 0 and INVALID_VALUE; no place for the result, INVALID_VALUE, nothing
//written. A tick of 835072 is 0001-01-01 and 835072 microseconds, 62135596800000000 is 1970-01-01, and a date's tick
//(2012-09-20, a leap day, the last microsecond of 1999, the last of 9999) comes back as that date, over another.
static auto rtcFileTimesAndTicks() -> void {
  KernelMachine m;
  auto put = [&](u32 at, std::array<u32, 7> fields) {
    for(u32 n = 0; n < 6; n++) m.system.memory.write(2, at + n * 2, fields[n]);
    m.system.memory.write(4, at + 12, fields[6]);
  };
  auto date = [&](u32 at) {
    std::array<u32, 7> fields;
    for(u32 n = 0; n < 6; n++) fields[n] = m.system.memory.read(2, at + n * 2);
    fields[6] = word(m, at + 12);
    return fields;
  };
  auto wide = [&](u32 at) { return word(m, at) | u64(word(m, at + 4)) << 32; };
  auto setWide = [&](u32 at, u64 value) {
    m.system.memory.write(4, at, u32(value));
    m.system.memory.write(4, at + 4, u32(value >> 32));
  };
  auto fileTime = [&](std::array<u32, 7> fields, u32 expected, u64 time) {
    put(R, fields);
    setWide(R + 0x20, u64(-1337));
    check(__LINE__, "a file time's result", m.call("sceRtcGetWin32FileTime", {R, R + 0x20}), expected);
    check(__LINE__, "a file time", wide(R + 0x20), time);
  };
  put(R, {1600, 1, 1, 0, 0, 0, 0});
  CHECK(m.call("sceRtcGetWin32FileTime", {R, 0}), Kernel::ErrorInvalidValue);
  fileTime({0, 0, 0, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  fileTime({2005, 11, 31, 13, 1, 0, 1}, 0, 127'779'156'600'000'010ull);
  fileTime({1601, 1, 1, 0, 0, 0, 0}, 0, 0);
  fileTime({1600, 1, 1, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  fileTime({1, 1, 1, 0, 0, 0, 0}, Kernel::ErrorInvalidValue, 0);
  auto ticked = [&](u64 tick, std::array<u32, 7> expected) {
    setWide(R + 0x20, tick);
    put(R + 0x40, {2010, 9, 20, 7, 12, 15, 500});
    check(__LINE__, "a tick's result", m.call("sceRtcSetTick", {R + 0x40, R + 0x20}), 0);
    check(__LINE__, "a tick's date", date(R + 0x40) == expected, true);
  };
  ticked(835'072, {1, 1, 1, 0, 0, 0, 835'072});
  ticked(62'135'596'800'000'000ull, {1970, 1, 1, 0, 0, 0, 0});
  for(auto fields : {std::array<u32, 7>{2012, 9, 20, 7, 12, 15, 500}, {2000, 2, 29, 12, 0, 0, 0},
                     {1999, 12, 31, 23, 59, 59, 999'999}, {9999, 12, 31, 23, 59, 59, 999'999}}) {
    put(R, fields);
    CHECK(m.call("sceRtcGetTick", {R, R + 0x20}), 0);
    ticked(wide(R + 0x20), fields);
  }
  CHECK(m.call("sceRtcSetTick", {0, R + 0x20}), Kernel::ErrorInvalidPointer);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

auto mediaTests() -> Tests {
  return {{"mpeg stubs", mpegStubs}, {"atrac stubs", atracStubs}, {"network off", networkOff},
          {"odds and ends of part 20", oddsAndEnds}, {"rtc file times and ticks", rtcFileTimesAndTicks},
          {"threads odds and ends of part 20", threadOddsAndEnds},
          {"threads dispatching held off", dispatchHeldOff},
          {"threads lightweight mutex deleted in a callback", lwMutexDeletedInCallback},
          {"fonts missing", fontsMissing}, {"threads kernel area zeroed", kernelArea}};
}

}
