//Sound output's channels (ares/psp/kernel/audio.cpp): reserving and releasing, the refusals, and the timing that
//paces games: a buffer takes its samples' time to play, 64 at a time, its first 64 taken as it arrives, and a
//blocking output waits for the channel's last buffer to finish (on the SRC channel, for its transfer to end, 100
//microseconds before that). Programs run on both engines. Expected values are pspautotests' (audio/*, intr/waits:
//results recorded on a PSP) where they have one.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results;
constexpr u32 Buffer = 0x0893'0000;
//a block of 64 samples at 44.1 kHz, in microseconds
constexpr double Block = 64.0 * 1'000'000 / 44'100;
//how long before an SRC buffer has been heard its slot frees, in microseconds: the first after an idle stretch
//retires this much short of its length, those chained after it a whole buffer later
constexpr double Lead = 100;
auto near(u32 measured, double expected) -> bool { return measured >= expected - 2 && measured <= expected + 30; }

//a / b rounded down (towards minus infinity), as a sample times a volume over full is defined to round
auto floorDiv(s64 a, s64 b) -> s64 { return a >= 0 ? a / b : -((-a + b - 1) / b); }
//a 16-bit sample's range
auto clamp16(s64 value) -> s64 { return std::clamp<s64>(value, -32768, 32767); }

//Moves the clock on to a cycle, lets what's due happen, and takes the output frames heard by then.
auto heard(KernelMachine& m, u64 cycle) -> std::vector<s16> {
  m.kernel.cycles = cycle;
  m.kernel.events();
  std::vector<s16> frames;
  m.kernel.audioOutput(frames);
  return frames;
}

//Writes stereo 16-bit pairs (left, right) from address on, sample n made by each(n).
auto putStereo(KernelMachine& m, u32 address, u32 count, const std::function<std::pair<s32, s32>(u32)>& each) {
  for(u32 n = 0; n < count; n++) {
    auto [left, right] = each(n);
    m.system.memory.write(2, address + n * 4, u16(left));
    m.system.memory.write(2, address + n * 4 + 2, u16(right));
  }
}
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
//buffer has retired since the last (the first at once: starting counts), so outputs return 0, 1 and 2 buffers' time
//in, less the 100 microseconds the transfers run ahead. While two are queued (the main thread waiting), another
//thread finds them counted, a third refused, and the channel not releasable; once the main thread is done, one
//plays. Sizes and rates out of range refused.
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
      double expected = n ? n * buffer - Lead : 0;
      CHECK(near(measured, expected), true);
      if(!near(measured, expected)) std::printf("  output %u returned at %u, not %.0f\n", n, measured, expected);
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
  CHECK(m.kernel.audio.src.rate, 22'050);
  CHECK(m.call("sceAudioOutput2Reserve", {1024}), Kernel::ErrorAudioChannelAlreadyReserved);
  CHECK(m.call("sceAudioSRCChRelease", {}), 0);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 22'000, 2}), Kernel::ErrorAudioInvalidFrequency);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 0, 1}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceAudioOutput2ChangeLength", {16}), Kernel::ErrorAudioSampleCount);
  CHECK(m.call("sceAudioOutput2ChangeLength", {512}), Kernel::ErrorAudioChannelNotReserved);
}

//100 blocking outputs of 64 samples, one after another on one channel from idle. The first is taken whole as it's
//handed over and the second goes into the slot it left free, so neither waits; from the third on, each waits for
//the boundary that takes the buffer before it. So output n returns n - 2 blocks in, and the hundredth 98 blocks
//(about 142 ms) in: as long as the samples take to play, never sooner.
static auto pacing() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.li(a0, 0); main.li(a1, 64); main.li(a2, 0);
    main.call("sceAudioChReserve");
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(s1, R);    //where each output's time goes (its result 0x400 on)
    main.li(s2, 100);  //outputs to go
    u32 loop = main.here();
    main.li(a0, 0); main.li(a1, 0x8000); main.li(a2, Buffer);
    main.call("sceAudioOutputBlocking");
    main.put(sw(v0, 0x400, s1));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s0));
    main.put(sw(v0, 0, s1));
    main.put(addiu(s1, s1, 4));
    main.put(addiu(s2, s2, -1));
    main.put(bne(s2, zero, int32_t(loop - (main.here() + 4)) / 4));
    main.put(nop);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    for(u32 n = 1; n <= 100; n++) {
      double expected = n <= 2 ? 0 : (n - 2) * Block;
      u32 measured = m.system.memory.read(4, R + (n - 1) * 4);
      CHECK(near(measured, expected), true);
      if(!near(measured, expected)) std::printf("  output %u returned at %u, not %.0f\n", n, measured, expected);
      CHECK(m.system.memory.read(4, R + 0x400 + (n - 1) * 4), 64);
    }
    CHECK(m.system.memory.read(4, R + 99 * 4) >= 98 * Block - 2, true);
    CHECK(m.notes.size(), 0);
  }
}

//Called directly, the clock moved on by hand. A second channel handed a buffer while the DMA runs isn't read until
//the next boundary. Once the last samples are taken (channel 1's, at boundary 16), the DMA stays busy for one more
//block: a buffer handed over then isn't read early either, and the boundary after takes its first block; once that
//one has gone (boundary 32), and the extra block too, the DMA is idle, and the next buffer loses its first block as
//it's handed over. Mono takes as long as stereo. A channel released while it plays isn't offered by the automatic
//search until it has drained, but naming it reserves it, its outputs BUSY meanwhile. Then the volume rules and the
//order of the checks, pspautotests' audio/sceaudio/output and audio/blocking/errors.
static auto mixerTimeline() -> void {
  KernelMachine m;
  auto at = [&](u64 block) { m.kernel.cycles = Kernel::CPUFrequency * 64 * block / 44'100; m.kernel.events(); };
  CHECK(m.call("sceAudioChReserve", {0, 1024, 0}), 0);
  CHECK(m.call("sceAudioChReserve", {1, 1024, 0x10}), 1);
  CHECK(m.call("sceAudioOutput", {0, 0x8000, Buffer}), 1024);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 1024 - 64);
  CHECK(m.call("sceAudioOutputPanned", {1, 0x8000, 0x8000, Buffer}), 1024);
  CHECK(m.call("sceAudioGetChannelRestLen", {1}), 1024);
  at(1);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 1024 - 128);
  CHECK(m.call("sceAudioGetChannelRestLen", {1}), 1024 - 64);
  at(16);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 0);
  CHECK(m.call("sceAudioGetChannelRestLength", {1}), 0);
  CHECK(m.kernel.audio.dma.running, true);
  CHECK(m.call("sceAudioOutput", {0, 0x8000, Buffer}), 1024);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 1024);
  at(17);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 1024 - 64);
  at(32);
  CHECK(m.call("sceAudioGetChannelRestLen", {0}), 0);
  CHECK(m.kernel.audio.dma.running, true);
  at(33);
  CHECK(m.kernel.audio.dma.running, false);
  CHECK(m.call("sceAudioOutput", {1, 0x8000, Buffer}), 1024);
  CHECK(m.call("sceAudioGetChannelRestLen", {1}), 1024 - 64);

  KernelMachine d;
  CHECK(d.call("sceAudioChReserve", {u32(-1), 2048, 0x10}), 7);
  CHECK(d.call("sceAudioOutputBlocking", {7, 0x8000, Buffer}), 2048);
  CHECK(d.call("sceAudioGetChannelRestLen", {7}), 0x7c0);
  CHECK(d.call("sceAudioChRelease", {7}), 0);
  CHECK(d.call("sceAudioChReserve", {u32(-1), 2048, 0}), 6);
  CHECK(d.call("sceAudioChRelease", {6}), 0);
  CHECK(d.call("sceAudioChReserve", {7, 2048, 0}), 7);
  CHECK(d.call("sceAudioOutput", {7, 0x8000, Buffer}), Kernel::ErrorAudioChannelBusy);
  CHECK(d.call("sceAudioChRelease", {7}), 0);
  d.kernel.cycles = Kernel::CPUFrequency * 64 * 30 / 44'100;
  d.kernel.events();
  CHECK(d.call("sceAudioGetChannelRestLen", {7}), 64);
  CHECK(d.call("sceAudioChReserve", {u32(-1), 2048, 0}), 6);
  CHECK(d.call("sceAudioChRelease", {6}), 0);
  d.kernel.cycles = Kernel::CPUFrequency * 64 * 31 / 44'100;  //its last block: 31 of them, mono as stereo
  d.kernel.events();
  CHECK(d.call("sceAudioChReserve", {0x8000'0000, 2048, 0}), 7);
  for(u32 n = 0; n < 7; n++) CHECK(d.call("sceAudioChReserve", {n, 64, 0}), n);
  CHECK(d.call("sceAudioChReserve", {u32(-1), 1000, 0}), Kernel::ErrorAudioNoChannels);  //the search comes first

  KernelMachine v;
  CHECK(v.call("sceAudioChReserve", {0, 64, 0}), 0);
  CHECK(v.call("sceAudioChangeChannelVolume", {0, 0x1234, 0x5678}), 0);
  CHECK(v.call("sceAudioOutput", {0, u32(-1), 0}), 64);  //a null buffer: plays nothing, starts nothing
  CHECK(v.call("sceAudioOutput", {0, 0x8000'0000, 0}), 64);
  CHECK(v.kernel.audio.dma.running, false);
  CHECK(v.kernel.audio.channels[0].leftVolume, 0x1234);
  CHECK(v.kernel.audio.channels[0].rightVolume, 0x5678);
  CHECK(v.call("sceAudioOutputPanned", {0, u32(-1), 0x100, 0}), 64);
  CHECK(v.kernel.audio.channels[0].leftVolume, 0x1234);
  CHECK(v.kernel.audio.channels[0].rightVolume, 0x100);
  CHECK(v.call("sceAudioOutputBlocking", {0, 0x7fff'ffff, 0}), Kernel::ErrorAudioInvalidVolume);
  CHECK(v.call("sceAudioOutputBlocking", {8, 0x10000, 0}), Kernel::ErrorAudioInvalidVolume);  //before the channel
  CHECK(v.call("sceAudioOutputPannedBlocking", {0, u32(-1), 0, 0}), Kernel::ErrorAudioInvalidVolume);
  CHECK(v.call("sceAudioOutputPannedBlocking", {0, 0xffff, 0xffff, 0}), 64);
  CHECK(v.kernel.audio.channels[0].leftVolume, 0xffff);
  CHECK(v.call("sceAudioOutputBlocking", {u32(-1), 0x8000, Buffer}), Kernel::ErrorAudioInvalidChannel);
  CHECK(v.call("sceAudioOutputBlocking", {5, 0x8000, Buffer}), Kernel::ErrorAudioChannelNotInitialized);
  CHECK(v.call("sceAudioChangeChannelVolume", {3, 0x4000, 0x4000}), 0);  //not reserved: no matter
  CHECK(v.call("sceAudioChangeChannelVolume", {8, 0x4000, 0x4000}), Kernel::ErrorAudioInvalidChannel);
  CHECK(v.call("sceAudioChangeChannelVolume", {8, 0x10000, 0}), Kernel::ErrorAudioInvalidVolume);
  CHECK(v.call("sceAudioSetChannelDataLen", {3, 1000}), Kernel::ErrorAudioSampleCount);
  CHECK(v.call("sceAudioSetChannelDataLen", {3, 1024}), Kernel::ErrorAudioChannelNotInitialized);
  CHECK(v.call("sceAudioChangeChannelConfig", {3, 0}), Kernel::ErrorAudioChannelNotReserved);
  CHECK(v.call("sceAudioChangeChannelConfig", {0, 0x11}), Kernel::ErrorAudioInvalidFormat);
  CHECK(v.call("sceAudioGetChannelRestLen", {3}), 0);
  CHECK(v.call("sceAudioGetChannelRestLength", {8}), Kernel::ErrorAudioInvalidChannel);
  CHECK(v.call("sceAudioChReserve", {0, 1024, 0}), Kernel::ErrorAudioInvalidChannel);
  CHECK(v.call("sceAudioChReserve", {1, 96, 0}), Kernel::ErrorAudioSampleCount);
  CHECK(v.call("sceAudioChReserve", {1, 64, u32(-1)}), Kernel::ErrorAudioInvalidFormat);
}

//Draining, by a program. A blocking output of a null buffer: at once on an idle channel; on a busy one, once its
//buffer has gone (15 blocks after it came), leaving its count as what remains to one rest length and not the other.
//The SRC channel: a 64-sample buffer retired 1351 microseconds after it's armed (100 short of its 1451, as its
//transfer ends), so a release 1 ms on is refused and one 2 ms on isn't; sceAudioOutput2ChangeLength counting an
//armed buffer at the new length; a null output waiting till everything armed has retired, then returning at once;
//buffers at 48 kHz playing in 2048/48000 s; a rate of 0 playing at 44.1 kHz; each first buffer from idle retiring
//100 microseconds short. (pspautotests' audio/blocking/contend, restlen, audio/output2/release, changelength,
//frequency.)
static auto draining() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.li(s1, R);
    auto store = [&](u32 offset) { main.put(sw(v0, offset, s1)); };
    auto clock = [&] { main.call("sceKernelGetSystemTimeLow"); main.put(addu(s0, v0, zero)); };
    auto since = [&](u32 offset) {
      main.call("sceKernelGetSystemTimeLow"); main.put(subu(v0, v0, s0)); store(offset);
    };
    auto output = [&](const char* function, u32 a, u32 b, u32 c, u32 offset) {
      main.li(a0, a); main.li(a1, b); main.li(a2, c); main.call(function); store(offset);
    };
    auto plain = [&](const char* function, u32 offset) { main.call(function); store(offset); };
    auto delay = [&](u32 microseconds) { main.li(a0, microseconds); main.call("sceKernelDelayThread"); };
    output("sceAudioChReserve", 0, 1024, 0, 0x80);
    output("sceAudioOutputBlocking", 0, 0x8000, 0, 0x84);       //idle: at once
    clock();
    output("sceAudioOutputBlocking", 0, 0x8000, Buffer, 0x88);
    output("sceAudioOutputBlocking", 0, 0x8000, 0, 0x8c);       //busy: once it has gone
    since(0x00);
    output("sceAudioGetChannelRestLen", 0, 0, 0, 0x90);
    output("sceAudioGetChannelRestLength", 0, 0, 0, 0x94);
    output("sceAudioChRelease", 0, 0, 0, 0x98);
    output("sceAudioOutput2Reserve", 64, 0, 0, 0x9c);
    output("sceAudioOutput2OutputBlocking", 0x8000, Buffer, 0, 0xa0);
    plain("sceAudioOutput2Release", 0xa4);
    delay(1000);
    plain("sceAudioOutput2Release", 0xa8);
    delay(1000);
    plain("sceAudioOutput2Release", 0xac);
    plain("sceAudioOutput2Release", 0xb0);
    output("sceAudioSRCChReserve", 4096, 44'100, 2, 0xb4);
    clock();
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0xb8);
    output("sceAudioOutput2ChangeLength", 64, 0, 0, 0xbc);
    plain("sceAudioOutput2GetRestSample", 0xc0);
    output("sceAudioSRCOutputBlocking", 0x8000, 0, 0, 0xc4);    //waits for the 4096 samples
    since(0x04);
    output("sceAudioSRCOutputBlocking", 0x8000, 0, 0, 0xc8);    //nothing armed: at once
    since(0x08);
    plain("sceAudioSRCChRelease", 0xcc);
    output("sceAudioSRCChReserve", 2048, 48'000, 2, 0xd0);
    clock();
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0xd4);
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0xd8);
    since(0x0c);
    output("sceAudioSRCOutputBlocking", 0x8000, 0, 0, 0xdc);
    since(0x10);
    plain("sceAudioSRCChRelease", 0xe0);
    output("sceAudioSRCChReserve", 2048, 0, 2, 0xe4);
    clock();
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0xe8);
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0xec);
    since(0x14);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency);
    CHECK(m.kernel.exited, true);
    auto result = [&](u32 offset) { return m.system.memory.read(4, R + offset); };
    CHECK(near(result(0x00), 15 * Block), true);
    CHECK(near(result(0x04), 4096.0 * 1'000'000 / 44'100 - Lead), true);
    CHECK(near(result(0x08), 4096.0 * 1'000'000 / 44'100 - Lead), true);
    CHECK(near(result(0x0c), 2048.0 * 1'000'000 / 48'000 - Lead), true);
    CHECK(near(result(0x10), 2 * 2048.0 * 1'000'000 / 48'000 - Lead), true);
    CHECK(near(result(0x14), 2048.0 * 1'000'000 / 44'100 - Lead), true);
    u32 expected[] = {
      0, 1024, 1024, 1024, 1024, 0, 0,                                         //0x80-0x98: the mixer channel
      0, 64, Kernel::ErrorAudioChannelAlreadyReserved, Kernel::ErrorAudioChannelAlreadyReserved, 0,
      Kernel::ErrorAudioChannelNotReserved,                                    //0x9c-0xb0: released after 2 ms
      0, 4096, 0, 64, 0, 0, 0,                                                 //0xb4-0xcc: drained
      0, 2048, 2048, 0, 0, 0, 2048, 2048,                                      //0xd0-0xec: 48 kHz, then 0
    };
    for(u32 n = 0; n < std::size(expected); n++) {
      check(__LINE__, ("result " + Kernel::hexWord(0x80 + n * 4)).c_str(), result(0x80 + n * 4), expected[n]);
    }
    CHECK(m.kernel.audio.src.rate, 44'100);
    CHECK(m.notes.size(), 0);
  }
}

//What waiting can't do (pspautotests' intr/waits). With interrupts held off: a blocking output into a channel's free
//slot returns as ever (0x40, twice from idle: the first block taken, the second into the slot), one that would wait
//gets CAN_NOT_WAIT, and an unreserved channel NOT_INIT first; SRC outputs get CAN_NOT_WAIT with their buffers armed
//all the same, BUSY once both are. Interrupts let back on, a null output finds both slots armed (BUSY); 2 ms on,
//the first has retired, and an output takes the completion that was never taken (it returns at once). In a vertical
//blank handler, the same with ILLEGAL_CONTEXT.
static auto contexts() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Semaphore = R + 0x100, Handled = R + 0x40;
    Assembler handler{m, 0x0880'3000};
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    for(u32 n = 0; n < 3; n++) {
      handler.li(a0, 2); handler.li(a1, 0x8000); handler.li(a2, Buffer);
      handler.call("sceAudioOutputBlocking");
      handler.li(t0, Handled); handler.put(sw(v0, n * 4, t0));
    }
    handler.li(a0, 0x8000); handler.li(a1, Buffer);
    handler.call("sceAudioSRCOutputBlocking");
    handler.li(t0, Handled); handler.put(sw(v0, 12, t0));
    handler.li(t0, Semaphore); handler.put(lw(a0, 0, t0)); handler.li(a1, 1);
    handler.call("sceKernelSignalSema");
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.put(jr(ra)); handler.put(nop);

    Assembler main{m, 0x0880'1000};
    main.li(s1, R);
    auto output = [&](const char* function, u32 a, u32 b, u32 c, u32 offset) {
      main.li(a0, a); main.li(a1, b); main.li(a2, c); main.call(function); main.put(sw(v0, offset, s1));
    };
    output("sceAudioChReserve", 0, 64, 0, 0x80);
    output("sceAudioSRCChReserve", 64, 44'100, 2, 0x84);
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    output("sceAudioOutputBlocking", 1, 0x8000, Buffer, 0x00);
    output("sceAudioOutputBlocking", 0, 0x8000, Buffer, 0x04);
    output("sceAudioOutputBlocking", 0, 0x8000, Buffer, 0x08);
    output("sceAudioOutputBlocking", 0, 0x8000, Buffer, 0x0c);
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0x10);
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0x14);
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0x18);
    output("sceAudioOutput2GetRestSample", 0, 0, 0, 0x1c);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCpuResumeIntr");
    output("sceAudioSRCOutputBlocking", 0x8000, 0, 0, 0x20);
    main.li(a0, 2000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s2, v0, zero));
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0x24);
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s2)); main.put(sw(v0, 0x28, s1));
    output("sceAudioSRCOutputBlocking", 0x8000, Buffer, 0, 0x2c);
    main.li(a0, 10'000);
    main.call("sceKernelDelayThread");
    output("sceAudioSRCChRelease", 0, 0, 0, 0x30);
    //the vertical blank handler's turn
    output("sceAudioChReserve", 2, 64, 0, 0x88);
    output("sceAudioSRCChReserve", 64, 44'100, 2, 0x8c);
    main.li(a0, m.string("s")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, Semaphore); main.put(sw(v0, 0, t0));
    output("sceKernelRegisterSubIntrHandler", 30, 1, 0x0880'3000, 0x90);
    output("sceKernelEnableSubIntr", 30, 1, 0, 0x94);
    main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelWaitSema");
    output("sceKernelReleaseSubIntrHandler", 30, 1, 0, 0x98);
    output("sceAudioOutput2GetRestSample", 0, 0, 0, 0x34);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto result = [&](u32 offset) { return m.system.memory.read(4, R + offset); };
    CHECK(result(0x00), Kernel::ErrorAudioChannelNotInitialized);
    CHECK(result(0x04), 64);
    CHECK(result(0x08), 64);
    CHECK(result(0x0c), Kernel::ErrorCanNotWait);
    CHECK(result(0x10), Kernel::ErrorCanNotWait);
    CHECK(result(0x14), Kernel::ErrorCanNotWait);
    CHECK(result(0x18), Kernel::ErrorAudioChannelBusy);
    CHECK(result(0x1c), 128);
    CHECK(result(0x20), Kernel::ErrorAudioChannelBusy);
    CHECK(result(0x24), 64);
    CHECK(result(0x28) < 30, true);  //at once: the completion pending since the first started
    CHECK(result(0x2c), Kernel::ErrorAudioChannelBusy);
    CHECK(result(0x30), 0);
    CHECK(result(0x34), 64);         //the handler's buffer, armed though it couldn't wait
    for(u32 offset : {0x80u, 0x84u, 0x8cu, 0x90u, 0x94u, 0x98u}) CHECK(result(offset), 0);
    CHECK(result(0x88), 2);
    CHECK(m.system.memory.read(4, Handled + 0), 64);
    CHECK(m.system.memory.read(4, Handled + 4), 64);
    CHECK(m.system.memory.read(4, Handled + 8), Kernel::ErrorIllegalContext);
    CHECK(m.system.memory.read(4, Handled + 12), Kernel::ErrorIllegalContext);
    CHECK(m.notes.size(), 0);
  }
}

//pspautotests' audio/output2/rest, as a PSP ran it: reserve 64 samples, hand a buffer over (from idle: at once,
//0x40), read the rest (0x40), then poll the clock and the rest until it changes, and report the microseconds in
//hundreds: "13XX" on a PSP, the slot freeing as the transfer ends, 100 microseconds before the buffer's 1451 have
//been heard; then release, and the rest of a channel not reserved. Through sceAudioOutput2* and sceAudioSRC* alike.
//The poll waits a microsecond a round where the PSP's spun: here the clock a system function reads moves on between
//the CPU's runs (each to the next thing due), not with each instruction, so a spinning loop reads it as it was when
//its run began.
static auto sourceRest() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.li(s1, R);
    for(u32 family = 0; family < 2; family++) {
      u32 at = family * 0x20;
      auto store = [&](u32 offset) { main.put(sw(v0, at + offset, s1)); };
      main.li(a0, 64); main.li(a1, 44'100); main.li(a2, 2);  //(sceAudioOutput2Reserve reads the count alone)
      main.call(family ? "sceAudioSRCChReserve" : "sceAudioOutput2Reserve");
      store(0x00);
      main.li(a0, 0x7fff); main.li(a1, Buffer);
      main.call(family ? "sceAudioSRCOutputBlocking" : "sceAudioOutput2OutputBlocking");
      store(0x04);
      main.call("sceAudioOutput2GetRestSample");
      store(0x08);
      main.call("sceKernelGetSystemTimeLow");
      main.put(addu(s0, v0, zero));
      u32 loop = main.here();  //a microsecond on, the clock, then the rest, until the rest isn't 0x40
      main.li(a0, 1);
      main.call("sceKernelDelayThread");
      main.call("sceKernelGetSystemTimeLow");
      main.put(addu(s2, v0, zero));
      main.call("sceAudioOutput2GetRestSample");
      main.li(t0, 0x40);
      main.put(beq(v0, t0, int32_t(loop - (main.here() + 4)) / 4));
      main.put(nop);
      store(0x0c);
      main.put(subu(v0, s2, s0));
      store(0x10);
      main.call(family ? "sceAudioSRCChRelease" : "sceAudioOutput2Release");
      store(0x14);
      main.call("sceAudioOutput2GetRestSample");
      store(0x18);
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    for(u32 at : {0x00u, 0x20u}) {
      auto result = [&](u32 offset) { return m.system.memory.read(4, R + at + offset); };
      CHECK(result(0x00), 0);
      CHECK(result(0x04), 0x40);
      CHECK(result(0x08), 0x40);
      CHECK(result(0x0c), 0);
      CHECK(result(0x10) / 100, 13);  //"Rest (after 13XXus): 00000000"
      if(result(0x10) / 100 != 13) std::printf("  the rest changed after %u microseconds\n", result(0x10));
      CHECK(result(0x14), 0);
      CHECK(result(0x18), Kernel::ErrorAudioChannelNotReserved);
    }
    CHECK(m.notes.size(), 0);
  }
}

//What a mixer channel plays, as heard. A stereo buffer handed over at cycle 0 starts the DMA there: its blocks fill
//output frames 0 on, each sample times its side's volume over 0x8000, rounded down (0x8000 as it is, 0x4000 at half:
//-1 becomes -1); then silence. A mono buffer is heard on both sides, each at its own volume; at 0xFFFF, nearly
//doubled, clamped to 16 bits at both ends. Two channels add up (10000 and 5000 are 15000), clamped too (30000 and
//30000 are 32767, -30000 and -30000 -32768). A channel handed a buffer while the DMA runs is heard from the next
//boundary (frame 64); one started later starts where it's handed over (1 ms on: frame 44, 44.1 rounded down). A null
//buffer plays nothing.
static auto mixing() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  putStereo(m, Buffer, 128, [](u32 n) { return std::pair{s32(n) * 100, -s32(n) * 50 - 1}; });
  CHECK(m.call("sceAudioChReserve", {0, 128, 0}), 0);
  CHECK(m.call("sceAudioOutputPanned", {0, 0x8000, 0x4000, Buffer}), 128);
  auto frames = heard(m, Kernel::CPUFrequency / 100);  //10 ms: 441 frames
  CHECK(frames.size(), 441 * 2);
  bool exact = frames.size() == 441 * 2;
  for(u32 n = 0; n < 441 && exact; n++) {
    s64 left = n < 128 ? n * 100 : 0, right = n < 128 ? floorDiv((-s64(n) * 50 - 1) * 0x4000, 0x8000) : 0;
    if(frames[n * 2] != left || frames[n * 2 + 1] != right) exact = false;
  }
  CHECK(exact, true);
  if(frames.size()) CHECK(frames[1], -1);

  //mono at 0x8000 and 0x2000; then, the DMA stopped, again at 0xFFFF from the frame it's handed over at (441)
  KernelMachine o;
  auto mono = [](u32 n) { return s64(n) * 600 - 19000; };
  for(u32 n = 0; n < 64; n++) o.system.memory.write(2, Buffer + n * 2, u16(mono(n)));
  CHECK(o.call("sceAudioChReserve", {1, 64, 0x10}), 1);
  CHECK(o.call("sceAudioOutputPanned", {1, 0x8000, 0x2000, Buffer}), 64);
  frames = heard(o, Kernel::CPUFrequency / 100);
  exact = frames.size() == 441 * 2;
  for(u32 n = 0; n < 64 && exact; n++) {
    if(frames[n * 2] != mono(n) || frames[n * 2 + 1] != floorDiv(mono(n) * 0x2000, 0x8000)) exact = false;
  }
  CHECK(exact, true);
  CHECK(o.call("sceAudioOutputPanned", {1, 0xffff, 0xffff, Buffer}), 64);
  frames = heard(o, Kernel::CPUFrequency / 50);
  exact = frames.size() == 441 * 2;
  for(u32 n = 0; n < 64 && exact; n++) {
    s64 loud = clamp16(floorDiv(mono(n) * 0xffff, 0x8000));
    if(frames[n * 2] != loud || frames[n * 2 + 1] != loud) exact = false;
  }
  CHECK(exact, true);
  if(frames.size() == 441 * 2) CHECK(frames[0] == -32768 && frames[63 * 2] == 32767 && frames[64 * 2] == 0, true);

  //two channels: 0 with three blocks (30000, 10000, 30000), 1 with two (5000, 30000) joining the running DMA
  KernelMachine t;
  putStereo(t, Buffer, 192, [](u32 n) { s32 v = n / 64 == 1 ? 10000 : 30000; return std::pair{v, -v}; });
  putStereo(t, Buffer + 0x1000, 128, [](u32 n) { s32 v = n < 64 ? 5000 : 30000; return std::pair{v, -v}; });
  CHECK(t.call("sceAudioChReserve", {0, 192, 0}), 0);
  CHECK(t.call("sceAudioChReserve", {1, 128, 0}), 1);
  CHECK(t.call("sceAudioOutput", {0, 0x8000, Buffer}), 192);
  CHECK(t.call("sceAudioOutput", {1, 0x8000, Buffer + 0x1000}), 128);
  frames = heard(t, Kernel::CPUFrequency / 100);
  CHECK(frames.size(), 441 * 2);
  if(frames.size() == 441 * 2) {
    for(auto [frame, left] : {std::pair{0, 30000}, {63, 30000}, {64, 15000}, {127, 15000}, {128, 32767},
                              {191, 32767}, {192, 0}}) {
      check(__LINE__, "a sum's left", frames[frame * 2], left);
      check(__LINE__, "a sum's right", frames[frame * 2 + 1], left == 32767 ? -32768 : -left);
    }
  }

  //a buffer handed over 1 ms on starts at frame 44; a null buffer plays nothing
  KernelMachine l;
  putStereo(l, Buffer, 64, [](u32) { return std::pair{1000, 2000}; });
  CHECK(l.call("sceAudioChReserve", {0, 64, 0}), 0);
  CHECK(l.call("sceAudioChReserve", {1, 64, 0}), 1);
  CHECK(l.call("sceAudioOutput", {1, 0x8000, 0}), 64);
  l.kernel.cycles = Kernel::CPUFrequency / 1000;
  CHECK(l.call("sceAudioOutput", {0, 0x8000, Buffer}), 64);
  frames = heard(l, Kernel::CPUFrequency / 100);
  CHECK(frames.size(), 441 * 2);
  if(frames.size() == 441 * 2) {
    CHECK(frames[43 * 2] == 0 && frames[44 * 2] == 1000 && frames[44 * 2 + 1] == 2000, true);
    CHECK(frames[107 * 2] == 1000 && frames[108 * 2] == 0, true);
  }
  CHECK(roundTrip(m), true);
  CHECK(memory.read(2, Buffer + 4), 100);  //(the buffer is read, never written)
}

//The SRC channel's samples converted to 44.1 kHz. At 22050 Hz a buffer of 64 fills 128 frames from where it's armed:
//its samples as they are on the even frames, halfway between neighbors on the odd ones, the last halfway to silence
//(nothing armed after it). At 44100 Hz a frame a sample; at 48000 Hz, 480 samples fill 441 frames, a ramp staying a
//ramp (within a step of its line); a volume of 0x4000 halves them. Two buffers armed one after the other play on
//without a gap, the first's last frame halfway to the second's first sample. The mixer and the SRC channel add up.
//(Called directly, with no thread to wait, an SRC output can't wait for its completion: CAN_NOT_WAIT, as from an
//interrupt handler, its buffer armed and played all the same.)
static auto sourceOutput() -> void {
  auto source = [](u32 n) { return std::pair{s32(n % 64) * 900 - 28000, 30000 - s32(n % 64) * 800}; };
  KernelMachine m;
  putStereo(m, Buffer, 64, source);
  CHECK(m.call("sceAudioSRCChReserve", {64, 22'050, 2}), 0);
  CHECK(m.call("sceAudioSRCOutputBlocking", {0x8000, Buffer}), Kernel::ErrorCanNotWait);
  auto frames = heard(m, Kernel::CPUFrequency / 100);
  CHECK(frames.size(), 441 * 2);
  CHECK(m.kernel.audio.src.armed, 0);  //retired: heard to its end
  bool exact = frames.size() == 441 * 2;
  for(u32 n = 0; n < 441 && exact; n++) {
    s64 left = 0, right = 0;
    if(n < 128) {
      auto [l0, r0] = source(n / 2);
      auto [l1, r1] = n / 2 + 1 < 64 ? source(n / 2 + 1) : std::pair{0, 0};
      left = n % 2 ? (l0 + l1) / 2 : l0, right = n % 2 ? (r0 + r1) / 2 : r0;
    }
    if(frames[n * 2] != left || frames[n * 2 + 1] != right) {
      std::printf("  frame %u: %d %d, not %lld %lld\n", n, frames[n * 2], frames[n * 2 + 1], left, right);
      exact = false;
    }
  }
  CHECK(exact, true);

  KernelMachine f;  //44.1 kHz: a frame a sample, at half volume
  putStereo(f, Buffer, 100, source);
  CHECK(f.call("sceAudioSRCChReserve", {100, 0, 2}), 0);
  CHECK(f.call("sceAudioSRCOutputBlocking", {0x4000, Buffer}), Kernel::ErrorCanNotWait);
  frames = heard(f, Kernel::CPUFrequency / 100);
  exact = frames.size() == 441 * 2;
  for(u32 n = 0; n < 441 && exact; n++) {
    auto [left, right] = n < 100 ? source(n) : std::pair{0, 0};
    if(frames[n * 2] != floorDiv(left, 2) || frames[n * 2 + 1] != floorDiv(right, 2)) exact = false;
  }
  CHECK(exact, true);

  KernelMachine h;  //48 kHz: 480 samples, 441 frames, a ramp still
  putStereo(h, Buffer, 480, [](u32 n) { return std::pair{s32(n) * 50, -s32(n) * 50}; });
  CHECK(h.call("sceAudioSRCChReserve", {480, 48'000, 2}), 0);
  CHECK(h.call("sceAudioSRCOutputBlocking", {0x8000, Buffer}), Kernel::ErrorCanNotWait);
  frames = heard(h, Kernel::CPUFrequency / 50);
  exact = frames.size() == 882 * 2;
  for(u32 n = 0; n < 882 && exact; n++) {
    double line = n < 441 ? n * 48'000.0 / 44'100 * 50 : 0;
    if(std::abs(frames[n * 2] - line) > 1 || std::abs(frames[n * 2 + 1] + line) > 1) exact = false;
  }
  CHECK(exact, true);

  //two buffers at 22050, the second armed as the first plays, and a mixer channel beside them
  KernelMachine c;
  putStereo(c, Buffer, 64, [](u32) { return std::pair{1000, -1000}; });
  putStereo(c, Buffer + 0x1000, 64, [](u32) { return std::pair{3000, -3000}; });
  putStereo(c, Buffer + 0x2000, 256, [](u32) { return std::pair{5, 7}; });
  CHECK(c.call("sceAudioSRCChReserve", {64, 22'050, 2}), 0);
  CHECK(c.call("sceAudioSRCOutputBlocking", {0x8000, Buffer}), Kernel::ErrorCanNotWait);
  CHECK(c.call("sceAudioSRCOutputBlocking", {0x8000, Buffer + 0x1000}), Kernel::ErrorCanNotWait);
  CHECK(c.call("sceAudioChReserve", {0, 256, 0}), 0);
  CHECK(c.call("sceAudioOutput", {0, 0x8000, Buffer + 0x2000}), 256);
  frames = heard(c, Kernel::CPUFrequency / 100);
  CHECK(frames.size(), 441 * 2);
  if(frames.size() == 441 * 2) {
    CHECK(frames[0] == 1005 && frames[1] == -993, true);
    CHECK(frames[126 * 2] == 1005 && frames[127 * 2] == 2005, true);  //the first's last frame: halfway to 3000
    CHECK(frames[128 * 2] == 3005 && frames[255 * 2] == 1505, true);  //the second's last: halfway to silence
    CHECK(frames[256 * 2] == 0 && frames[256 * 2 + 1] == 0, true);
  }
  CHECK(roundTrip(m), true);
  CHECK(roundTrip(c), true);
}

//The output carried in a state: a machine part way through a mixer buffer and an SRC buffer, saved, loads into
//another that makes the same state; both then play on to the same frames.
static auto outputState() -> void {
  KernelMachine m;
  putStereo(m, Buffer, 1024, [](u32 n) { return std::pair{s32(n % 300) * 90, -s32(n % 200) * 70}; });
  CHECK(m.call("sceAudioChReserve", {0, 1024, 0}), 0);
  CHECK(m.call("sceAudioOutput", {0, 0x7000, Buffer}), 1024);
  CHECK(m.call("sceAudioSRCChReserve", {1024, 11'025, 2}), 0);
  CHECK(m.call("sceAudioSRCOutputBlocking", {0x9000, Buffer}), Kernel::ErrorCanNotWait);
  auto before = heard(m, Kernel::CPUFrequency / 200 + 12345);  //5 ms and some: part way through both
  CHECK(before.size() > 200, true);
  auto state = saveState(m);
  KernelMachine n;
  CHECK(loadState(n, state), true);
  CHECK(saveState(n) == state, true);
  auto after = heard(m, Kernel::CPUFrequency / 10);
  CHECK(heard(n, Kernel::CPUFrequency / 10) == after, true);
  CHECK(after.size() > 4000, true);
  CHECK(std::any_of(after.begin(), after.end(), [](s16 sample) { return sample != 0; }), true);
}

auto audioTests() -> Tests {
  return {
    {"audio channels", channels}, {"audio blocking timing", blockingTiming}, {"audio src channel", sourceChannel},
    {"audio 64-sample pacing", pacing}, {"audio mixer timeline", mixerTimeline}, {"audio draining", draining},
    {"audio waits refused", contexts}, {"audio src rest", sourceRest}, {"audio mixed", mixing},
    {"audio src heard", sourceOutput}, {"audio output in states", outputState},
  };
}

}
