//Sound output's channels (ares/psp/kernel/audio.cpp): reserving and releasing, the refusals, and the timing that
//paces games: a buffer takes its samples' time to play, 64 at a time, its first 64 taken as it arrives, and a
//blocking output waits for the channel's last buffer to finish. Programs run on both engines.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results;
constexpr u32 Buffer = 0x0893'0000;
//a block of 64 samples at 44.1 kHz, in microseconds
constexpr double Block = 64.0 * 1'000'000 / 44'100;
auto near(u32 measured, double expected) -> bool { return measured >= expected - 2 && measured <= expected + 30; }
}

//Reserving: the highest free channel for -1, a count that's a multiple of 64 from 64 to 65472, stereo or mono; a
//channel taken or out of range refused. Outputs: a second while the first plays is busy; what's left counts the first
//block gone already; volumes and formats checked; releasing what isn't reserved refused.
static auto channels() -> void {
  KernelMachine m;
  CHECK(m.call("sceAudioChReserve", {u32(-1), 1024, 0}), 7);
  CHECK(m.call("sceAudioChReserve", {u32(-1), 1024, 0x10}), 6);
  CHECK(m.call("sceAudioChReserve", {7, 1024, 0}), Kernel::ErrorAudioInvalidChannel);
  CHECK(m.call("sceAudioChReserve", {8, 1024, 0}), Kernel::ErrorAudioInvalidChannel);
  CHECK(m.call("sceAudioChReserve", {0, 1000, 0}), Kernel::ErrorAudioSampleCount);
  CHECK(m.call("sceAudioChReserve", {0, 0, 0}), Kernel::ErrorAudioSampleCount);
  CHECK(m.call("sceAudioChReserve", {0, 65536, 0}), Kernel::ErrorAudioSampleCount);
  CHECK(m.call("sceAudioChReserve", {0, 65472, 0}), 0);
  CHECK(m.call("sceAudioChReserve", {1, 64, 1}), Kernel::ErrorAudioInvalidFormat);
  CHECK(m.call("sceAudioOutput", {1, 0x8000, Buffer}), Kernel::ErrorAudioChannelNotInitialized);
  CHECK(m.call("sceAudioOutputPanned", {7, 0x10000, 0, Buffer}), Kernel::ErrorAudioInvalidVolume);
  CHECK(m.call("sceAudioOutputPannedBlocking", {7, u32(-1), 0, Buffer}), Kernel::ErrorAudioInvalidVolume);
  CHECK(m.call("sceAudioOutput", {7, 0x8000, Buffer}), 1024);
  CHECK(m.call("sceAudioGetChannelRestLen", {7}), 1024 - 64);
  CHECK(m.call("sceAudioGetChannelRestLength", {7}), 1024 - 64);
  CHECK(m.call("sceAudioOutput", {7, 0x8000, Buffer}), Kernel::ErrorAudioChannelBusy);
  CHECK(m.call("sceAudioChangeChannelConfig", {7, 0x10}), Kernel::ErrorAudioChannelBusy);
  CHECK(m.call("sceAudioChangeChannelConfig", {6, 0x00}), 0);
  CHECK(m.call("sceAudioChangeChannelConfig", {6, 0x20}), Kernel::ErrorAudioInvalidFormat);
  CHECK(m.call("sceAudioChangeChannelVolume", {6, 0x4000, u32(-1)}), 0);
  CHECK(m.kernel.audio.channels[6].leftVolume, 0x4000);
  CHECK(m.call("sceAudioChangeChannelVolume", {6, 0x10000, 0}), Kernel::ErrorAudioInvalidVolume);
  CHECK(m.call("sceAudioSetChannelDataLen", {6, 512}), 0);
  CHECK(m.kernel.audio.channels[6].sampleCount, 512);
  CHECK(m.call("sceAudioSetChannelDataLen", {5, 512}), Kernel::ErrorAudioChannelNotInitialized);
  CHECK(m.call("sceAudioSetChannelDataLen", {6, 500}), Kernel::ErrorAudioSampleCount);
  //a buffer at address 0 is taken, plays nothing and counts in one rest length but not the other
  CHECK(m.call("sceAudioOutput", {6, 0x8000, 0}), 512);
  CHECK(m.call("sceAudioGetChannelRestLen", {6}), 512);
  CHECK(m.call("sceAudioGetChannelRestLength", {6}), 0);
  CHECK(m.call("sceAudioChRelease", {6}), 0);
  CHECK(m.call("sceAudioChRelease", {6}), Kernel::ErrorAudioChannelNotReserved);
  CHECK(m.call("sceAudioChRelease", {9}), Kernel::ErrorAudioInvalidChannel);
  //the first buffer's 15 blocks left play out (the clock moved on by hand: no thread runs to pass the time)
  m.kernel.cycles = Kernel::CPUFrequency * 64 * 14 / 44'100;  //when block 14 is mixed
  m.kernel.events();
  CHECK(m.call("sceAudioGetChannelRestLen", {7}), 64);
  m.kernel.cycles = Kernel::CPUFrequency * 64 * 15 / 44'100;
  m.kernel.events();
  CHECK(m.call("sceAudioGetChannelRestLen", {7}), 0);
  CHECK(m.call("sceAudioOutput", {7, 0x8000, Buffer}), 1024);
}

//A thread hands over four buffers of 1024 samples, blocking: the first at once, the second once the first has played
//(its first block went as it arrived: 15 blocks later), then one every 1024 samples' time (16 blocks). A second
//thread trying to wait on the same channel is told it's busy; on another channel it isn't.
static auto blockingTiming() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler other{m, 0x0880'2000};  //lower priority: runs while main waits
    other.li(a0, 0); other.li(a1, 0x8000); other.li(a2, 0x8000); other.li(a3, Buffer);
    other.call("sceAudioOutputPannedBlocking");
    other.li(t0, R); other.put(sw(v0, 0x40, t0));
    other.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, 0); main.li(a1, 1024); main.li(a2, 0);
    main.call("sceAudioChReserve");
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(s1, R);
    for(u32 n = 0; n < 4; n++) {
      main.li(a0, 0); main.li(a1, 0x8000); main.li(a2, Buffer);
      main.call("sceAudioOutputBlocking");
      main.put(sw(v0, 0x20 + n * 4, s1));
      main.call("sceKernelGetSystemTimeLow");
      main.put(subu(v0, v0, s0));
      main.put(sw(v0, n * 4, s1));
      if(n == 1) {  //now it waits: the other thread tries too
        main.li(a0, m.string("other")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
        main.li(t0, 0); main.li(t1, 0);
        main.call("sceKernelCreateThread");
        main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
        main.call("sceKernelStartThread");
      }
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    double expected[] = {0, 15 * Block, 31 * Block, 47 * Block};
    for(u32 n = 0; n < 4; n++) {
      u32 measured = m.system.memory.read(4, R + n * 4);
      CHECK(near(measured, expected[n]), true);
      if(!near(measured, expected[n])) std::printf("  buffer %u at %u, not %.0f\n", n, measured, expected[n]);
      CHECK(m.system.memory.read(4, R + 0x20 + n * 4), 1024);
    }
    CHECK(m.system.memory.read(4, R + 0x40), Kernel::ErrorAudioChannelBusy);
    CHECK(m.notes.size(), 0);
  }
}

//The SRC channel (sceAudioOutput2): two buffers at most, one after the other; a blocking output returns once a
//buffer has finished since the last (the first at once: starting counts), so outputs return 0, 1 and 2 buffers'
//time in. While two are queued (the main thread waiting), another thread finds them counted, a third refused, and
//the channel not releasable; once the main thread is done, one plays. Sizes and rates out of range refused.
static auto sourceChannel() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler other{m, 0x0880'2000};
    other.li(s1, R);
    other.call("sceAudioOutput2GetRestSample");
    other.put(sw(v0, 0x30, s1));
    other.li(a0, 0x8000); other.li(a1, Buffer);
    other.call("sceAudioOutput2OutputBlocking");
    other.put(sw(v0, 0x34, s1));
    other.call("sceAudioOutput2Release");
    other.put(sw(v0, 0x38, s1));
    other.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, 1024);
    main.call("sceAudioOutput2Reserve");
    main.li(a0, m.string("other")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //runs as main waits on its second output
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(s1, R);
    for(u32 n = 0; n < 3; n++) {
      main.li(a0, 0x8000); main.li(a1, Buffer);
      main.call("sceAudioOutput2OutputBlocking");
      main.put(sw(v0, 0x20 + n * 4, s1));
      main.call("sceKernelGetSystemTimeLow");
      main.put(subu(v0, v0, s0));
      main.put(sw(v0, n * 4, s1));
    }
    main.call("sceAudioOutput2GetRestSample");
    main.put(sw(v0, 0x3c, s1));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    double buffer = 1024.0 * 1'000'000 / 44'100;
    for(u32 n = 0; n < 3; n++) {
      u32 measured = m.system.memory.read(4, R + n * 4);
      CHECK(near(measured, n * buffer), true);
      if(!near(measured, n * buffer)) std::printf("  output %u returned at %u, not %.0f\n", n, measured, n * buffer);
      CHECK(m.system.memory.read(4, R + 0x20 + n * 4), 1024);
    }
    CHECK(m.system.memory.read(4, R + 0x30), 2048);
    CHECK(m.system.memory.read(4, R + 0x34), Kernel::ErrorAudioChannelBusy);
    CHECK(m.system.memory.read(4, R + 0x38), Kernel::ErrorAudioChannelAlreadyReserved);
    CHECK(m.system.memory.read(4, R + 0x3c), 1024);
  }
  KernelMachine m;
  CHECK(m.call("sceAudioOutput2Reserve", {16}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceAudioOutput2Reserve", {4112}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceAudioOutput2OutputBlocking", {0x8000, Buffer}), Kernel::ErrorAudioChannelNotReserved);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 22'050, 2}), 0);
  CHECK(m.kernel.audio.source.frequency, 22'050);
  CHECK(m.call("sceAudioOutput2Reserve", {1024}), Kernel::ErrorAudioChannelAlreadyReserved);
  CHECK(m.call("sceAudioSRCChRelease", {}), 0);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 22'000, 2}), Kernel::ErrorAudioInvalidFrequency);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 0, 1}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceAudioOutput2ChangeLength", {16}), Kernel::ErrorAudioSampleCount);
  CHECK(m.call("sceAudioOutput2ChangeLength", {512}), Kernel::ErrorAudioChannelNotReserved);
}

auto audioTests() -> Tests {
  return {
    {"audio channels", channels}, {"audio blocking timing", blockingTiming}, {"audio src channel", sourceChannel},
  };
}

}
