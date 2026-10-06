//sceSasCore (ares/psp/kernel/sas.cpp): what its functions take and refuse, as pspautotests' audio/sascore recorded
//on a PSP; envelopes grain by grain against the same recordings; voices ending as their samples run out or their
//release comes down to 0, and the end flags refreshed by __sceSasCore; in a program on both engines too; playing
//voices given new samples; and what the voices sound like: VAG blocks decoded to the samples the format's
//definition gives and the PSP recorded, PCM voices at their pitches, volumes, envelopes, the output modes, mixing
//into the game's buffer. Each group's machine, saved at its end, loads into another that makes the same state.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 Core = 0x0890'4000, Out = 0x0893'0000, Samples = 0x0894'0000, R = KernelMachine::Results;
constexpr u32 ErrorGrain = 0x8042'0001, ErrorVoices = 0x8042'0002, ErrorMode = 0x8042'0003;
constexpr u32 ErrorRate = 0x8042'0004, ErrorAddress = 0x8042'0005, ErrorVoice = 0x8042'0010;
constexpr u32 ErrorPitch = 0x8042'0012, ErrorCurve = 0x8042'0013, ErrorSize = 0x8042'0014, ErrorLoop = 0x8042'0015;
constexpr u32 ErrorState = 0x8042'0016, ErrorVolume = 0x8042'0018, ErrorAdsr = 0x8042'0019, ErrorPcm = 0x8042'001a;

//The curves, as pspsdk names them.
enum : u32 { Increase = 0, Decrease = 1, Bent = 2, ExponentRev = 3, Exponent = 4, Direct = 5 };

//The grain written at Out: frame n's left and right (stereo), or the n-th 16-bit sample (multichannel's planes).
auto left(KernelMachine& m, u32 frame) -> u16 { return m.system.memory.read(2, Out + frame * 4); }
auto right(KernelMachine& m, u32 frame) -> u16 { return m.system.memory.read(2, Out + frame * 4 + 2); }
auto plane(KernelMachine& m, u32 n) -> u16 { return m.system.memory.read(2, Out + n * 2); }

//audio/sascore's setup for a voice heard at once: its attack reaching the top in one sample, and its decay, sustain
//and release holding it there (direct, at the top)
auto loud(KernelMachine& m, u32 voice) -> void {
  m.call("__sceSasSetADSRmode", {Core, voice, 15, Increase, Direct, Direct, Direct});
  m.call("__sceSasSetADSR", {Core, voice, 15, 0x4000'0000, 0x4000'0000, 0x4000'0000, 0x4000'0000});
}

//audio/sascore's PCM samples: (short)(65535 - n), so -1, -2, -3...
auto pcmSamples(KernelMachine& m, u32 address, u32 count) -> void {
  for(u32 n = 0; n < count; n++) m.system.memory.write(2, address + n * 2, u16(65535 - n));
}
}

//What each function takes and refuses, as audio/sascore's tests recorded: __sceSasInit's checks in their order;
//volumes, pitches, VAG and PCM sizes and loops; the ADSR rates, each phase's curves, __sceSasSetSimpleADSR's words;
//the effect's settings; the grain and output mode; pausing.
static auto sasSettings() -> void {
  KernelMachine m;
  auto init = [&](u32 core, u32 grain, u32 voices, u32 mode, u32 rate) {
    return m.call("__sceSasInit", {core, grain, voices, mode, rate});
  };
  CHECK(init(0, 1024, 32, 0, 44100), ErrorAddress);
  CHECK(init(Core + 1, 1024, 32, 0, 44100), ErrorAddress);
  CHECK(init(Core, 1024, 32, 0, 44100), 0);
  CHECK(init(Core, 1024, 32, 0, 44100), 0);  //again
  for(u32 grain : {u32(-0x400), u32(-1), 0u, 1u, 8u, 0x20u, 0x5cu, 0xc00u, 0x1000u}) {
    CHECK(init(Core, grain, 32, 0, 44100), ErrorGrain);
  }
  for(u32 grain : {0x40u, 0x100u, 0x700u, 0x7c0u, 0x800u}) CHECK(init(Core, grain, 32, 0, 44100), 0);
  for(u32 voices : {u32(-16), u32(-1), 0u, 33u}) CHECK(init(Core, 1024, voices, 0, 44100), ErrorVoices);
  for(u32 voices : {1u, 16u, 31u, 32u}) CHECK(init(Core, 1024, voices, 0, 44100), 0);
  for(u32 mode : {u32(-2), u32(-1), 2u, 3u}) CHECK(init(Core, 1024, 32, mode, 44100), ErrorMode);
  CHECK(init(Core, 1024, 32, 1, 44100), 0);
  for(u32 rate : {u32(-1), 0u, 22050u, 44099u, 48000u}) CHECK(init(Core, 1024, 32, 0, rate), ErrorRate);
  CHECK(init(0, 1024, 0, 9, 1), ErrorAddress);  //the address first
  CHECK(init(Core, 3, 0, 9, 1), ErrorGrain);    //then the grain, the voices, the mode
  CHECK(init(Core, 1024, 0, 9, 1), ErrorVoices);
  CHECK(init(Core, 1024, 1, 9, 1), ErrorMode);
  CHECK(init(Core, 128, 32, 0, 44100), 0);
  CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'ffff);  //every voice ended before the first grain

  auto volume = [&](u32 voice, s32 l, s32 r, s32 el, s32 er) {
    return m.call("__sceSasSetVolume", {Core, voice, u32(l), u32(r), u32(el), u32(er)});
  };
  CHECK(volume(0, -0x1000, 0x1000, 0, 0), 0);
  CHECK(volume(0, -0x1001, 0, 0, 0), ErrorVolume);
  CHECK(volume(0, 0, 0x1001, 0, 0), ErrorVolume);
  CHECK(volume(0, 0, 0, -0x1001, 0), ErrorVolume);
  CHECK(volume(0, 0, 0, 0, 0x1001), ErrorVolume);
  CHECK(volume(32, 0, 0, 0, 0), ErrorVoice);
  CHECK(volume(u32(-1), 0, 0, 0, 0), ErrorVoice);
  for(u32 pitch : {0u, 1u, 0x1000u, 0x4000u}) CHECK(m.call("__sceSasSetPitch", {Core, 0, pitch}), 0);
  for(u32 pitch : {u32(-1), 0x4001u, 0x8000'0001u}) CHECK(m.call("__sceSasSetPitch", {Core, 0, pitch}), ErrorPitch);

  auto vag = [&](u32 size, u32 loop) { return m.call("__sceSasSetVoice", {Core, 0, Samples, size, loop}); };
  for(u32 size : {0x10u, 0x30u, 0x100u, 0x10000u, 0xffff'ffc0u, 0x8000'0000u}) CHECK(vag(size, 0), 0);
  for(u32 size : {0u, 1u, 15u, 0xffu, 0x101u, 0xffff'ffd1u, 0xffff'ffffu}) CHECK(vag(size, 0), ErrorSize);
  CHECK(vag(0x100, 1), 0);
  for(u32 loop : {u32(-1), 2u, 0x8000'0001u}) CHECK(vag(0x100, loop), ErrorLoop);
  CHECK(vag(1, 2), ErrorSize);  //the size before the loop
  auto pcm = [&](u32 samples, u32 loop) { return m.call("__sceSasSetVoicePCM", {Core, 0, Samples, samples, loop}); };
  for(u32 samples : {1u, 256u, 0x10000u}) CHECK(pcm(samples, u32(-1)), 0);
  for(u32 samples : {0u, u32(-1), 0x10001u}) CHECK(pcm(samples, 0), ErrorPcm);
  for(u32 loop : {u32(-1), 0u, 255u, 0x8000'0001u}) CHECK(pcm(256, loop), 0);
  for(u32 loop : {256u, 0x10000u, 0x4000'0001u}) CHECK(pcm(256, loop), ErrorLoop);
  CHECK(m.call("__sceSasSetNoise", {Core, 0, 0x3f}), 0);
  CHECK(m.call("__sceSasSetNoise", {Core, 0, 0x40}), 0x8042'0011);

  auto& voice = m.kernel.sas.voices[0];
  auto adsr = [&](u32 which, s32 a, s32 d, s32 s, s32 r) {
    return m.call("__sceSasSetADSR", {Core, 0, which, u32(a), u32(d), u32(s), u32(r)});
  };
  CHECK(adsr(15, 0x1000, 0x2000, 0x3000, 0x4000), 0);
  CHECK(voice.rates[0] == 0x1000 && voice.rates[3] == 0x4000, true);
  CHECK(adsr(15, -1, 0, 0, 0), ErrorAdsr);
  CHECK(adsr(15, 0, 0, 0, s32(0x8000'0000)), ErrorAdsr);
  CHECK(voice.rates[0], 0x1000);  //none set
  CHECK(adsr(0, -1, -1, -1, -1), 0);
  CHECK(adsr(~15u, -1, -1, -1, -1), 0);
  CHECK(adsr(15, 0x7fff'ffff, 0x7fff'ffff, 0x7fff'ffff, 0x7fff'ffff), 0);
  auto mode = [&](u32 which, u32 curve) {
    return m.call("__sceSasSetADSRmode", {Core, 0, which, curve, curve, curve, curve});
  };
  //(which: 1 attack, 2 decay, 4 sustain, 8 release) each curve taken where pspautotests found it taken
  bool taken[4][6] = {{1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1}, {1, 1, 1, 1, 1, 1}, {0, 1, 0, 1, 0, 1}};
  for(u32 phase = 0; phase < 4; phase++) {
    for(u32 curve = 0; curve < 6; curve++) {
      check(__LINE__, "a curve", mode(1 << phase, curve), taken[phase][curve] ? 0 : ErrorCurve);
    }
    CHECK(mode(1 << phase, 6), ErrorCurve);
    CHECK(mode(1 << phase, u32(-1)), ErrorCurve);
  }
  CHECK(mode(2, 0x8000'0001), 0);  //the top bit ignored
  CHECK(voice.curves[1], 1);
  CHECK(mode(0, 9), 0);
  CHECK(m.call("__sceSasSetADSRmode", {Core, 0, 15, Direct, Direct, Direct, Direct}), ErrorCurve);

  auto simple = [&](u32 first, u32 second) { return m.call("__sceSasSetSimpleADSR", {Core, 0, first, second}); };
  auto rates = [&](s32 a, s32 d, s32 s, s32 r) {
    return voice.rates[0] == a && voice.rates[1] == d && voice.rates[2] == s && voice.rates[3] == r;
  };
  auto curves = [&](u32 a, u32 d, u32 s, u32 r) {
    return voice.curves[0] == a && voice.curves[1] == d && voice.curves[2] == s && voice.curves[3] == r;
  };
  CHECK(simple(0, 0), 0);
  CHECK(curves(0, 3, 0, 1) && rates(0x1c00'0000, 0x7fff'ffff, 0x1c00'0000, 0x1000'0000), true);
  CHECK(voice.sustainLevel, 0x0400'0000);
  CHECK(simple(0, 1 << 13), ErrorCurve);
  struct Attack { u32 rate; s32 value; };
  for(auto [rate, value] : {Attack{1, 0x1800'0000}, {2, 0x1400'0000}, {0x60, 0x1c}, {0x69, 6}, {0x6a, 5}, {0x6b, 4},
                            {0x6c, 3}, {0x6f, 2}, {0x70, 1}, {0x7e, 1}, {0x7f, 0}}) {
    simple(rate << 8, 0);
    check(__LINE__, "an attack rate", voice.rates[0], value);
    simple(rate << 8 | 0x8000, 0);
    check(__LINE__, "a bent attack", voice.curves[0] == Bent && voice.rates[0] == value, true);
    simple(0, rate << 6 | 3 << 14);  //exponent rev's sustain steps a quarter as far
    s32 quarter = rate == 0x7f ? 0 : rate == 0x60 ? 7 : rate <= 2 ? value / 4 : 1;
    check(__LINE__, "an exponential sustain rate", voice.curves[2] == ExponentRev && voice.rates[2] == quarter, true);
  }
  for(u32 decay = 1; decay < 16; decay++) {
    simple(decay << 4, 0);
    check(__LINE__, "a decay rate", voice.rates[1], s32(0x8000'0000u >> decay));
  }
  simple(0xf, 0);
  CHECK(voice.sustainLevel, 0x4000'0000);
  struct Release { u32 rate; s32 linear, exponential; };
  for(auto [rate, linear, exponential] : {Release{1, 0x0800'0000, 0x4000'0000}, {9, 0x80000, 0x40'0000},
                                          {0x10, 0x1000, 0x8000}, {0x1a, 4, 0x20}, {0x1c, 1, 8}, {0x1d, 1, 4},
                                          {0x1e, 0x4000'0000, 2}, {0x1f, 0, 0}}) {
    simple(0, rate);
    check(__LINE__, "a linear release", voice.curves[3] == Decrease && voice.rates[3] == linear, true);
    simple(0, rate | 0x20);
    check(__LINE__, "an exponential release", voice.curves[3] == ExponentRev && voice.rates[3] == exponential, true);
  }
  CHECK(simple(0x800f, 0x9fe0), 0);  //setadsr's "ARG"
  CHECK(curves(2, 3, 2, 3) && rates(0x1c00'0000, 0x7fff'ffff, 0, 0x7fff'ffff), true);

  CHECK(m.call("__sceSasRevType", {Core, u32(-1)}), 0);
  CHECK(m.call("__sceSasRevType", {Core, 8}), 0);
  CHECK(m.call("__sceSasRevType", {Core, 9}), 0x8042'0020);
  CHECK(m.call("__sceSasRevParam", {Core, 128, 128}), 0);
  CHECK(m.call("__sceSasRevParam", {Core, 129, 0}), 0x8042'0022);
  CHECK(m.call("__sceSasRevParam", {Core, 0, 129}), 0x8042'0021);
  CHECK(m.call("__sceSasRevEVOL", {Core, 0x1000, 0x1000}), 0);
  CHECK(m.call("__sceSasRevEVOL", {Core, 0x1001, 0}), 0x8042'0023);
  CHECK(m.call("__sceSasRevVON", {Core, 1, 1}), 0);
  CHECK(m.call("__sceSasSetGrain", {Core, 0x5c}), ErrorGrain);
  CHECK(m.call("__sceSasSetGrain", {Core, 512}), 0);
  CHECK(m.call("__sceSasGetGrain", {Core}), 512);
  CHECK(m.call("__sceSasSetOutputmode", {Core, 2}), ErrorMode);
  CHECK(m.call("__sceSasSetOutputmode", {Core, 1}), 0);
  CHECK(m.call("__sceSasGetOutputmode", {Core}), 1);
  for(auto [bits, pause, flags] : {std::tuple{1u, 1u, 1u}, {1, 1, 1}, {1, 0, 0}, {1, 2, 1}, {1, 0x8000'0000, 1},
                                   {u32(-1), 1, u32(-1)}, {6, 0, 0xffff'fff9}}) {
    CHECK(m.call("__sceSasSetPause", {Core, bits, pause}), 0);
    check(__LINE__, "the pause flags", m.call("__sceSasGetPauseFlag", {Core}), flags);
  }
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Envelopes grain by grain, as audio/sascore recorded them: keyed on, a voice starts 32 samples into the next grain
//(keyon, getheight, keyoff); released; an attack reaching the top within a grain and decaying for the rest of it,
//and a bent one (adsrcurve); exponent rev's decay, exactly; keying on and off refused as recorded; a paused voice
//standing still; a release reaching 0 ending its voice, its end flag set by the next grain.
static auto sasEnvelopes() -> void {
  KernelMachine m;
  auto core = [&] { CHECK(m.call("__sceSasCore", {Core, Out}), 0); };
  auto height = [&](u32 voice = 0) { return m.call("__sceSasGetEnvelopeHeight", {Core, voice}); };
  auto setup = [&](u32 grain, u32 attack, s32 rate, u32 decay = Decrease, s32 decayRate = 0x1000) {
    m.call("__sceSasInit", {Core, grain, 32, 0, 44100});
    m.call("__sceSasSetVoicePCM", {Core, 0, Samples, 0x10000, 0});
    m.call("__sceSasSetADSRmode", {Core, 0, 15, attack, decay, Decrease, Decrease});
    m.call("__sceSasSetADSR", {Core, 0, 15, u32(rate), u32(decayRate), 0x1000, 0x1000});
  };
  setup(128, Increase, 0x1000);
  CHECK(m.call("__sceSasSetKeyOff", {Core, 0}), ErrorState);  //not on
  CHECK(m.call("__sceSasSetKeyOn", {Core, 0}), 0);
  CHECK(m.call("__sceSasSetKeyOn", {Core, 0}), ErrorState);   //on already
  CHECK(height(32), ErrorVoice);
  core();
  CHECK(height(), 0x60000);  //96 samples
  CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'fffe);
  for(u32 n = 0; n < 3; n++) core();
  CHECK(height(), 0x1e'0000);
  CHECK(m.call("__sceSasSetKeyOff", {Core, 0}), 0);
  core();
  CHECK(height(), 0x16'0000);
  for(u32 n = 0; n < 3; n++) core();
  CHECK(height(), 0);  //released to 0: ended
  CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'ffff);
  CHECK(m.call("__sceSasSetKeyOn", {Core, 0}), 0);           //ended by itself: it may be keyed on again
  m.call("__sceSasSetPause", {Core, 1, 1});
  CHECK(m.call("__sceSasSetKeyOff", {Core, 0}), ErrorState);  //paused
  core();
  CHECK(height(), 0);  //paused: it doesn't move
  m.call("__sceSasSetPause", {Core, 1, 0});
  core();
  CHECK(height(), 0x60000);
  u32 heights = 0x0895'0000;
  m.system.memory.fill(heights, 0xcc, 33 * 4);
  CHECK(m.call("__sceSasGetAllEnvelopeHeights", {Core, heights}), 0);
  CHECK(m.system.memory.read(4, heights) == 0x60000 && m.system.memory.read(4, heights + 31 * 4) == 0, true);
  CHECK(m.system.memory.read(4, heights + 32 * 4), 0xcccc'cccc);

  //adsrcurve, at a grain of 64: an attack of 0x1000000 reaching the top 32 samples into its second grain, decaying
  //by 1 a sample from there
  setup(64, Increase, 0x100'0000, Decrease, 1);
  m.call("__sceSasSetKeyOn", {Core, 0});
  u32 expected[] = {0x2000'0000, 0x3fff'ffe0, 0x3fff'ffa0};
  for(u32 value : expected) core(), check(__LINE__, "an attack's heights", height(), value);
  //a bent attack at 0x100000: 0x2e000000 after 12 grains, bent to 0x308c0000 in the 13th, 0x318c0000 in the 14th;
  //at the top in the 29th, decaying from there
  setup(64, Bent, 0x10'0000, Decrease, 1);
  m.call("__sceSasSetKeyOn", {Core, 0});
  for(u32 n = 0; n < 12; n++) core();
  CHECK(height(), 0x2e00'0000);
  core();
  CHECK(height(), 0x308c'0000);
  core();
  CHECK(height(), 0x318c'0000);
  for(u32 n = 0; n < 15; n++) core();
  CHECK(height(), 0x3fff'ffdd);
  //exponent rev's decay from the top at 0x100000 (an attack of 0x2000000 reaches it as the first grain ends)
  setup(64, Increase, 0x200'0000, ExponentRev, 0x10'0000);
  m.call("__sceSasSetKeyOn", {Core, 0});
  core();
  CHECK(height(), 0x4000'0000);
  for(u32 value : {0x3f01'f557u, 0x3e07'db13u, 0x3d11'a192u, 0x3c1f'396eu}) {
    core();
    check(__LINE__, "exponent rev's heights", height(), value);
  }
  setup(64, Increase, 0x200'0000, ExponentRev, 0x4000'0000);
  m.call("__sceSasSetKeyOn", {Core, 0});
  for(u32 value : {0x4000'0000u, 9u, 0u}) core(), check(__LINE__, "a fast exponent rev", height(), value);
  //a direct release sets the height to its rate: 0 ends the voice at once
  m.call("__sceSasSetADSRmode", {Core, 0, 8, 0, 0, 0, Direct});
  m.call("__sceSasSetADSR", {Core, 0, 8, 0, 0, 0, 0});
  m.call("__sceSasSetKeyOff", {Core, 0});
  core();
  CHECK(height() == 0 && m.call("__sceSasGetEndFlag", {Core}) == 0xffff'ffff, true);
  //the buffer: a grain of the voices, stereo (silence: every voice has ended); mixing adds them to what it holds, at
  //volumes 0x1000 (as it is)
  m.system.memory.fill(Out, 0x55, 64 * 4 + 4);
  core();
  CHECK(m.system.memory.read(4, Out) == 0 && m.system.memory.read(4, Out + 63 * 4) == 0, true);
  CHECK(m.system.memory.read(4, Out + 64 * 4), 0x5555'5555);
  m.system.memory.fill(Out, 0x55, 64 * 4);
  CHECK(m.call("__sceSasCoreWithMix", {Core, Out, 0x1000, 0x1000}), 0);
  CHECK(m.system.memory.read(4, Out), 0x5555'5555);
  CHECK(m.call("__sceSasCore", {Core, 0}), ErrorAddress);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//A program keys voices on and makes grains (256 samples), writing down the end flags after each: a VAG voice of four
//unmarked blocks (112 samples) ends in its first grain; one whose last block marks a loop keeps playing; a PCM voice
//of 300 samples ends in its second (224 samples in the first, after the 32 it waits); one at twice the pitch in its
//first; a looping PCM voice goes on. On both engines.
static auto sasVoicesEnd() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    for(u32 block = 0; block < 4; block++) {  //VAG data: four blocks, unmarked; then the same with a loop at the end
      m.system.memory.write(1, Samples + block * 16 + 1, 0);
      m.system.memory.write(1, Samples + 64 + block * 16 + 1, block == 3 ? 3 : 0);
    }
    Assembler main{m, 0x0880'1000};
    auto call = [&](const char* name, std::initializer_list<u32> arguments) {
      u32 n = 0;
      for(u32 value : arguments) main.li(n < 4 ? a0 + n : t0 + n - 4, value), n++;
      main.call(name);
    };
    call("__sceSasInit", {Core, 256, 32, 0, 44100});
    call("__sceSasSetVoice", {Core, 0, Samples, 64, 1});
    call("__sceSasSetVoice", {Core, 1, Samples + 64, 64, 1});
    call("__sceSasSetVoicePCM", {Core, 2, Samples, 300, u32(-1)});
    call("__sceSasSetVoicePCM", {Core, 3, Samples, 300, u32(-1)});
    call("__sceSasSetPitch", {Core, 3, 0x2000});
    call("__sceSasSetVoicePCM", {Core, 4, Samples, 300, 100});
    for(u32 voice = 0; voice < 5; voice++) {
      call("__sceSasSetADSR", {Core, voice, 15, 0x1000, 0, 0, 0});
      call("__sceSasSetKeyOn", {Core, voice});
    }
    for(u32 grain = 0; grain < 3; grain++) {
      call("__sceSasCore", {Core, Out});
      call("__sceSasGetEndFlag", {Core});
      main.li(t0, R + grain * 4); main.put(sw(v0, 0, t0));
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    u32 others = 0xffff'ffe0;
    CHECK(m.system.memory.read(4, R), others | 1 << 0 | 1 << 3);
    CHECK(m.system.memory.read(4, R + 4), others | 1 << 0 | 1 << 2 | 1 << 3);
    CHECK(m.system.memory.read(4, R + 8), others | 1 << 0 | 1 << 2 | 1 << 3);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Playing voices given new samples: three PCM voices 2016 samples into 65536; given 100 samples, the first goes on
//from its last and ends at the next grain; given 300 looping from 100, the second goes on from where its loop would
//have taken it; given VAG data, the third starts from its beginning, as does a VAG voice then given PCM samples. The
//machine's state, saved after each change, loads into another machine and makes the same state (the first voice,
//left past its samples, had made the machine's own state refused).
static auto sasNewSamples() -> void {
  KernelMachine m;
  auto& voices = m.kernel.sas.voices;
  auto core = [&] { CHECK(m.call("__sceSasCore", {Core, Out}), 0); };
  m.call("__sceSasInit", {Core, 256, 32, 0, 44100});
  for(u32 voice : {0u, 1u, 2u}) {
    m.call("__sceSasSetVoicePCM", {Core, voice, Samples, 0x10000, u32(-1)});
    m.call("__sceSasSetKeyOn", {Core, voice});
  }
  for(u32 n = 0; n < 8; n++) core();
  CHECK(voices[0].position, u64(8 * 256 - 32) << 12);
  CHECK(m.call("__sceSasSetVoicePCM", {Core, 0, Samples, 100, u32(-1)}), 0);
  CHECK(voices[0].position, (u64(100) << 12) - 1);
  CHECK(roundTrip(m), true);
  CHECK(m.call("__sceSasSetVoicePCM", {Core, 1, Samples, 300, 100}), 0);
  CHECK(voices[1].position, u64(100 + (2016 - 300) % 200) << 12);
  CHECK(roundTrip(m), true);
  CHECK(m.call("__sceSasSetVoice", {Core, 2, Samples, 0x1000, 0}), 0);
  CHECK(voices[2].position, 0);
  CHECK(roundTrip(m), true);
  core();
  CHECK(m.call("__sceSasGetEndFlag", {Core}) & 7, 1);  //the first ended; the second loops, the third plays on
  CHECK(voices[1].position, u64(100 + (216 + 256 - 300) % 200) << 12);
  CHECK(voices[2].position, u64(256) << 12);
  CHECK(m.call("__sceSasSetVoicePCM", {Core, 2, Samples, 200, u32(-1)}), 0);
  CHECK(voices[2].position, 0);
  CHECK(roundTrip(m), true);
  CHECK(m.notes.size(), 0);
}

//pspautotests' audio/sascore/vag, as a PSP played it: 16 blocks, block n's first byte (filter << 4) | n, its flags 0
//for the first and the byte given for the rest, its samples' 4 bits 3, 2, 3, 3, 3, 4, 3, 5... (bytes (j << 4) | 3);
//keyed on at grain 512 with the attack reaching the top at once, and frames 32-39 written: silence, then the first
//seven samples decoded, a sample late. Every filter 0-15 with flags 0, then filter 0 with flags 3 (loops: still
//playing), 7, 0x41, 1 and 0x87 (the data runs out in the grain either way). The figures are vag.expected's.
static auto sasVagRecorded() -> void {
  KernelMachine m;
  constexpr u32 Data = Samples + 0x8000;
  m.call("__sceSasInit", {Core, 128, 32, 1, 44100});
  auto play = [&](u32 filter, u32 flags) {
    for(u32 block = 0; block < 16; block++) {
      m.system.memory.write(1, Data + block * 16 + 0, filter << 4 | block);
      m.system.memory.write(1, Data + block * 16 + 1, block ? flags : 0);
      for(u32 j = 2; j < 16; j++) m.system.memory.write(1, Data + block * 16 + j, j << 4 | 3);
    }
    m.call("__sceSasSetKeyOff", {Core, 0});
    loud(m, 0);
    m.call("__sceSasSetGrain", {Core, 512});
    m.call("__sceSasSetOutputmode", {Core, 0});
    m.call("__sceSasSetVoice", {Core, 0, Data, 0x100, 1});
    m.call("__sceSasSetKeyOn", {Core, 0});
    m.system.memory.fill(Out, 0xdd, 512 * 4);
    CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  };
  using Frames = std::array<u16, 8>;
  Frames zero = {0, 0x3000, 0x2000, 0x3000, 0x3000, 0x3000, 0x4000, 0x3000};
  std::array<Frames, 16> recorded = {{
    zero,
    {0, 0x3000, 0x4d00, 0x7830, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x7640, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x6980, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x7b80, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    zero,
    zero,
    {0, 0x3000, 0x4700, 0x69b0, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x4940, 0x6d73, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x4d00, 0x1a70, 0xb265, 0xb39b, 0x7fff, 0x7fff},
    zero,
    {0, 0x3000, 0x2000, 0xebc0, 0x0280, 0x4ccb, 0x3c72, 0xc2cf},
    zero,
    {0, 0x3000, 0x2180, 0x8f0c, 0xbb68, 0x7fff, 0x7fff, 0x8000},
    {0, 0x3000, 0x7dc0, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    {0, 0x3000, 0x2000, 0xbec0, 0xe480, 0x7fff, 0x7fff, 0x8000},
  }};
  for(u32 filter = 0; filter < 16; filter++) {
    play(filter, 0);
    std::string name = "filter " + std::to_string(filter);
    for(u32 n = 0; n < 8; n++) {
      check(__LINE__, (name + "'s left").c_str(), left(m, 32 + n), recorded[filter][n]);
      check(__LINE__, (name + "'s right").c_str(), right(m, 32 + n), recorded[filter][n]);
    }
    CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'ffff);
  }
  for(auto [flags, ended] : {std::pair{0x03u, 0xffff'fffeu}, {0x07, 0xffff'ffff}, {0x41, 0xffff'ffff},
                             {0x01, 0xffff'ffff}, {0x87, 0xffff'ffff}}) {
    play(0, flags);
    for(u32 n = 0; n < 8; n++) check(__LINE__, "a flagged block's samples", left(m, 32 + n), zero[n]);
    check(__LINE__, "a flagged block's end", m.call("__sceSasGetEndFlag", {Core}), ended);
  }
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//VAG blocks decoded to the samples the format's definition gives (the SPU's ADPCM, as psx-spx describes it, with the
//PSP's guess rounded down), worked out by hand. Each voice plays two blocks at pitch 0x1000, heard at once (frame
//32 silence, then each sample a frame late: block 1's first sample at frame 61). Block 0 (filter 0: no guess) sets
//the two samples block 1 guesses from, its last two.
//- shift 0, 4 bits 7, -8, 1, -1: 28672, -32768, 4096, -4096 (each times 4096); its last two 4096 and 8192;
//- then filter 1 (60/64 of the last), shift 12 (corrections as they are), 4 bits 0, 3, 0: 8192 * 60 / 64 = 7680;
//  3 + 7680 * 60 / 64 = 7203; 7203 * 60 / 64 = 6752.8, rounded down: 6752;
//- after -7 (shift 12), filter 1's guess -420 / 64 = -6.56 rounds down to -7 (not up to -6);
//- after -4096 and 4096, filter 2 (115/64 of the last less 52/64 of the one before): 4096 * 115 + 4096 * 52 =
//  684032, / 64 = 10688; with 4 bits -8: -8 + (10688 * 115 - 4096 * 52) / 64 = -8 + 15877 = 15869;
//- after 0 and 28672, filter 4 (122/64, 60/64) and a shift of 13 (as 9: 4096 >> 9 = 8 for a 1): 8 + 28672 * 122 /
//  64 = 54664, kept within 16 bits: 32767;
//- shifts 13 and 15 act as 9: 1 is 8, -1 is -8, 7 is 56.
//The block marked as the end (flags 7) ends the voice: its last sample, a frame late, isn't heard (frame 88 on is
//silence). A loop: blocks 0-2 starting with 4096, 8192 and 12288 (filter 0, the rest 0), block 1 marked where the
//loop starts (6), block 2 its end (3): after block 2, block 1 again (frame 117: 8192). A block whose flags byte is
//0x41 goes on (music.vag's header, read as blocks, has it, and played on in audio/sascore/vag's recording).
static auto sasVagDecoded() -> void {
  struct Case { std::vector<std::pair<u32, u32>> nibbles0, nibbles1; u8 header0, header1; u8 flags1;
                std::vector<std::pair<u32, s32>> heard; };
  std::vector<Case> cases = {
    {{{0, 7}, {1, 8}, {2, 1}, {3, 15}, {26, 1}, {27, 2}}, {{1, 3}}, 0x00, 0x1c, 7,
     {{32, 0}, {33, 28672}, {34, -32768}, {35, 4096}, {36, -4096}, {37, 0}, {58, 0}, {59, 4096}, {60, 8192},
      {61, 7680}, {62, 7203}, {63, 6752}, {88, 0}}},
    {{{27, 9}}, {}, 0x0c, 0x1c, 7, {{60, -7}, {61, -7}, {62, -7}}},
    {{{26, 15}, {27, 1}}, {{1, 8}}, 0x00, 0x2c, 7, {{59, -4096}, {60, 4096}, {61, 10688}, {62, 15869}}},
    {{{27, 7}}, {{0, 1}}, 0x00, 0x4d, 7, {{60, 28672}, {61, 32767}, {62, 32767}}},
    {{}, {{0, 1}, {1, 15}, {2, 7}}, 0x00, 0x0d, 7, {{61, 8}, {62, -8}, {63, 56}}},
    {{}, {{0, 1}, {1, 15}, {2, 7}}, 0x00, 0x0f, 0x41, {{61, 8}, {62, -8}, {63, 56}, {89, 0}}},
  };
  for(auto& c : cases) {
    KernelMachine m;
    auto put = [&](u32 block, u8 header, u8 flags, const std::vector<std::pair<u32, u32>>& nibbles) {
      u32 at = Samples + block * 16;
      m.system.memory.write(1, at, header);
      m.system.memory.write(1, at + 1, flags);
      for(auto [index, bits] : nibbles) {
        u32 byte = m.system.memory.read(1, at + 2 + index / 2);
        m.system.memory.write(1, at + 2 + index / 2, byte | bits << (index & 1) * 4);
      }
    };
    put(0, c.header0, 0, c.nibbles0);
    put(1, c.header1, c.flags1, c.nibbles1);
    m.call("__sceSasInit", {Core, 128, 32, 0, 44100});
    loud(m, 0);
    m.call("__sceSasSetVoice", {Core, 0, Samples, c.flags1 == 7 ? 32u : 64u, 0});
    m.call("__sceSasSetKeyOn", {Core, 0});
    CHECK(m.call("__sceSasCore", {Core, Out}), 0);
    for(auto [frame, sample] : c.heard) {
      check(__LINE__, ("frame " + std::to_string(frame) + "'s left").c_str(), left(m, frame), u16(sample));
      check(__LINE__, ("frame " + std::to_string(frame) + "'s right").c_str(), right(m, frame), u16(sample));
    }
    CHECK(m.call("__sceSasGetEndFlag", {Core}) & 1, c.flags1 == 7 ? 1 : 0);
    CHECK(roundTrip(m), true);
  }

  KernelMachine m;  //the loop
  for(u32 block = 0; block < 3; block++) {
    m.system.memory.write(1, Samples + block * 16 + 1, block == 1 ? 6 : block == 2 ? 3 : 0);
    m.system.memory.write(1, Samples + block * 16 + 2, block + 1);
  }
  m.call("__sceSasInit", {Core, 128, 32, 0, 44100});
  loud(m, 0);
  m.call("__sceSasSetVoice", {Core, 0, Samples, 48, 1});
  m.call("__sceSasSetKeyOn", {Core, 0});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  for(auto [frame, sample] : {std::pair{33u, 4096}, {34, 0}, {61, 8192}, {89, 12288}, {116, 0}, {117, 8192}}) {
    check(__LINE__, ("loop frame " + std::to_string(frame)).c_str(), left(m, frame), u16(sample));
  }
  CHECK(m.call("__sceSasGetEndFlag", {Core}) & 1, 0);
  CHECK(roundTrip(m), true);
}

//PCM voices heard. pspautotests' audio/sascore/pcm, as a PSP played it: 256 samples (-1, -2, -3...), keyed on at
//grain 512, frame 0x20 silence (the envelope's 0 as it starts), 0x11e the 254th sample, 0x120 the loop's first (from
//0: -1; from 254: -255) or, not looping, silence and the voice ended. Then, as chosen (no recording): at pitch
//0x2000 every other sample; at 0x800 each sample then the point halfway to the next (a ramp of 60s: 30s between).
static auto sasPcmHeard() -> void {
  KernelMachine m;
  pcmSamples(m, Samples, 0x2000);
  m.call("__sceSasInit", {Core, 128, 32, 1, 44100});
  for(auto [loop, at120, ended] : {std::tuple{0u, 0xffffu, 0xffff'fffeu}, {254, 0xff01, 0xffff'fffe},
                                   {u32(-1), 0, 0xffff'ffff}}) {
    m.call("__sceSasSetKeyOff", {Core, 0});
    loud(m, 0);
    m.call("__sceSasSetGrain", {Core, 512});
    m.call("__sceSasSetOutputmode", {Core, 0});
    m.call("__sceSasSetVoicePCM", {Core, 0, Samples, 256, loop});
    m.call("__sceSasSetKeyOn", {Core, 0});
    m.system.memory.fill(Out, 0xdd, 512 * 4);
    CHECK(m.call("__sceSasCore", {Core, Out}), 0);
    CHECK(left(m, 0x20) == 0 && right(m, 0x20) == 0, true);
    CHECK(left(m, 0x11e) == 0xff01 && right(m, 0x11e) == 0xff01, true);
    check(__LINE__, "frame 0x120", left(m, 0x120), at120);
    check(__LINE__, "frame 0x120's right", right(m, 0x120), at120);
    check(__LINE__, "the end flags", m.call("__sceSasGetEndFlag", {Core}), ended);
  }
  //every sample at 0x1000 (frame 32 + n is sample n, past the silent first)
  bool exact = true;
  for(u32 n = 1; n < 256; n++) if(left(m, 32 + n) != u16(65535 - n)) exact = false;
  CHECK(exact, true);
  CHECK(roundTrip(m), true);

  KernelMachine p;
  for(u32 n = 0; n < 512; n++) p.system.memory.write(2, Samples + n * 2, n * 60);
  p.call("__sceSasInit", {Core, 256, 32, 0, 44100});
  for(u32 pitch : {0x2000u, 0x800u}) {
    p.call("__sceSasSetKeyOff", {Core, 0});
    loud(p, 0);
    p.call("__sceSasSetPitch", {Core, 0, pitch});
    p.call("__sceSasSetVoicePCM", {Core, 0, Samples, 512, u32(-1)});
    p.call("__sceSasSetKeyOn", {Core, 0});
    CHECK(p.call("__sceSasCore", {Core, Out}), 0);
    exact = true;
    for(u32 n = 1; n < 224; n++) if(left(p, 32 + n) != (pitch == 0x2000 ? n * 120 : n * 30)) exact = false;
    check(__LINE__, pitch == 0x2000 ? "pitch 0x2000" : "pitch 0x800", exact, true);
  }
  CHECK(roundTrip(p), true);
}

//pspautotests' audio/sascore/outputmode, as a PSP played it: a PCM voice (-1, -2, -3...) at volumes 0x1000, 0xC00
//(dry), 0x800 and 0x400 (to the effect), heard dry and wet, the effect's volumes 0. Stereo: frames 0x20-0x22 are 0,
//-2 and -3 on the left, the right three quarters of them rounded down (0, -2, -3), frames 0x120-0x122 the loop's -1,
//-2, -3, and past the grain the buffer as it was. Multichannel: four planes of 512, dry left, dry right, wet left,
//wet right, the 32nd sample of each at the four volumes (-33 whole, three quarters, half, a quarter: -33, -25, -17,
//-9) from 16-bit sample 64 of its plane on. __sceSasCoreWithMix with volumes 0 and 0 in stereo: the buffer's 0xdddd
//gone, the voice alone; in multichannel refused (0x80000004), the buffer untouched, the voice still playing. The
//figures are outputmode.expected's.
static auto sasOutputModes() -> void {
  KernelMachine m;
  pcmSamples(m, Samples, 0x2000);
  m.call("__sceSasInit", {Core, 128, 32, 1, 44100});
  auto play = [&](u32 mode, bool mix) {
    m.call("__sceSasSetKeyOff", {Core, 0});
    loud(m, 0);
    m.call("__sceSasRevVON", {Core, 1, 1});
    m.call("__sceSasRevEVOL", {Core, 0, 0});
    m.call("__sceSasRevParam", {Core, 0, 0});
    m.call("__sceSasRevType", {Core, 0});
    m.call("__sceSasSetVolume", {Core, 0, 0x1000, 0x0c00, 0x0800, 0x0400});
    m.call("__sceSasSetGrain", {Core, 512});
    m.call("__sceSasSetOutputmode", {Core, mode});
    m.call("__sceSasSetVoicePCM", {Core, 0, Samples, 256, 0});
    m.call("__sceSasSetKeyOn", {Core, 0});
    m.system.memory.fill(Out, 0xdd, 4096 * 4);
    return mix ? m.call("__sceSasCoreWithMix", {Core, Out, 0, 0}) : m.call("__sceSasCore", {Core, Out});
  };
  //(printed as audio/sascore printed them: 16-bit samples 2i and 2i + 1, for i = 0x20-0x22, 0x120-0x122...)
  auto pairs = [&](std::vector<std::pair<u16, u16>> expected) {
    u32 at[] = {0x20, 0x21, 0x22, 0x120, 0x121, 0x122, 0x220, 0x221, 0x222, 0x320, 0x321, 0x322};
    for(u32 n = 0; n < 12; n++) {
      check(__LINE__, ("pair " + Kernel::hexWord(at[n])).c_str(), plane(m, at[n] * 2), expected[n].first);
      check(__LINE__, ("pair " + Kernel::hexWord(at[n])).c_str(), plane(m, at[n] * 2 + 1), expected[n].second);
    }
  };
  std::vector<std::pair<u16, u16>> stereo = {{0, 0}, {0xfffe, 0xfffe}, {0xfffd, 0xfffd}, {0xffff, 0xffff},
    {0xfffe, 0xfffe}, {0xfffd, 0xfffd}, {0xdddd, 0xdddd}, {0xdddd, 0xdddd}, {0xdddd, 0xdddd}, {0xdddd, 0xdddd},
    {0xdddd, 0xdddd}, {0xdddd, 0xdddd}};
  CHECK(play(0, false), 0);
  pairs(stereo);
  CHECK(play(1, false), 0);
  pairs({{0xffdf, 0xffde}, {0xffdd, 0xffdc}, {0xffdb, 0xffda}, {0xffe7, 0xffe6}, {0xffe5, 0xffe5}, {0xffe4, 0xffe3},
         {0xffef, 0xffef}, {0xffee, 0xffee}, {0xffed, 0xffed}, {0xfff7, 0xfff7}, {0xfff7, 0xfff7}, {0xfff6, 0xfff6}});
  CHECK(play(0, true), 0);
  pairs(stereo);
  CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'fffe);
  CHECK(play(1, true), 0x8000'0004);
  pairs(std::vector<std::pair<u16, u16>>(12, {0xdddd, 0xdddd}));
  CHECK(m.call("__sceSasGetEndFlag", {Core}), 0xffff'fffe);
  CHECK(roundTrip(m), true);
}

//Voices mixed, as chosen where no recording shows it. Two voices add up, clamped to 16 bits (30000 and 30000:
//32767); a negative volume turns a voice upside down; an envelope half way up halves a sample, rounded down (-101:
//-51); a paused voice is silent. The dry sound off (__sceSasRevVON(0, ...)) leaves silence; the wet sound alone,
//through an effect (type 4), passes through at the effect's volumes (0x800 and 0x1000) after the voice's effect
//volumes (0x1000 and 0x800): a 1000 is 500 on both sides; with no effect chosen (type -1), silence.
//__sceSasCoreWithMix at 0x800 halves what the buffer held before adding the voices.
static auto sasMixed() -> void {
  KernelMachine m;
  for(u32 n = 0; n < 256; n++) {
    m.system.memory.write(2, Samples + n * 2, 30000);
    m.system.memory.write(2, Samples + 0x1000 + n * 2, u16(-101));
    m.system.memory.write(2, Samples + 0x2000 + n * 2, 1000);
  }
  m.call("__sceSasInit", {Core, 64, 32, 0, 44100});
  for(u32 voice : {0u, 1u}) {
    loud(m, voice);
    m.call("__sceSasSetVoicePCM", {Core, voice, Samples, 256, 0});
    m.call("__sceSasSetKeyOn", {Core, voice});
  }
  m.call("__sceSasSetVolume", {Core, 1, 0x1000, u32(-0x1000), 0, 0});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 40) == 32767 && right(m, 40) == 0, true);  //30000 + 30000; 30000 - 30000
  m.call("__sceSasSetPause", {Core, 2, 1});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 10) == 30000 && right(m, 10) == 30000, true);
  //voice 2 alone (0 and 1 paused from here on), its attack reaching half way in one sample, the top in the next
  m.call("__sceSasSetPause", {Core, 3, 1});
  m.call("__sceSasSetADSRmode", {Core, 2, 15, Increase, Direct, Direct, Direct});
  m.call("__sceSasSetADSR", {Core, 2, 15, 0x2000'0000, 0x4000'0000, 0x4000'0000, 0x4000'0000});
  m.call("__sceSasSetVoicePCM", {Core, 2, Samples + 0x1000, 256, 0});
  m.call("__sceSasSetKeyOn", {Core, 2});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 32) == 0 && left(m, 33) == u16(-51) && left(m, 34) == u16(-101), true);
  m.call("__sceSasSetVoicePCM", {Core, 2, Samples + 0x2000, 256, 0});
  m.call("__sceSasRevVON", {Core, 0, 0});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 5) == 0 && right(m, 5) == 0, true);
  m.call("__sceSasRevVON", {Core, 0, 1});
  m.call("__sceSasRevType", {Core, 4});
  m.call("__sceSasRevEVOL", {Core, 0x800, 0x1000});
  m.call("__sceSasSetVolume", {Core, 2, 0x1000, 0x1000, 0x1000, 0x800});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 5) == 500 && right(m, 5) == 500, true);
  m.call("__sceSasRevType", {Core, u32(-1)});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  CHECK(left(m, 5) == 0 && right(m, 5) == 0, true);
  m.call("__sceSasRevVON", {Core, 1, 0});
  for(u32 n = 0; n < 64; n++) m.system.memory.write(4, Out + n * 4, 0xc000'4000);  //16384 left, -16384 right
  CHECK(m.call("__sceSasCoreWithMix", {Core, Out, 0x800, 0x1000}), 0);
  CHECK(left(m, 5) == 8192 + 1000 && right(m, 5) == u16(-16384 + 1000), true);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//A VAG voice part way through a block and a PCM voice part way between samples (pitch 0x1234), saved after a grain,
//load into another machine that makes the same state, and both machines make the same next grains.
static auto sasVoicesInStates() -> void {
  KernelMachine m;
  for(u32 n = 0; n < 0x400; n++) m.system.memory.write(1, Samples + n, u8(n * 37 + 11));  //blocks of noise...
  for(u32 block = 0; block < 64; block++) m.system.memory.write(1, Samples + block * 16 + 1, 0);  //...unflagged
  for(u32 n = 0; n < 1000; n++) m.system.memory.write(2, Samples + 0x1000 + n * 2, u16(n * 61));
  m.call("__sceSasInit", {Core, 192, 32, 0, 44100});
  for(u32 voice : {0u, 1u}) loud(m, voice);
  m.call("__sceSasSetVoice", {Core, 0, Samples, 0x400, 0});
  m.call("__sceSasSetVoicePCM", {Core, 1, Samples + 0x1000, 1000, 0});
  m.call("__sceSasSetPitch", {Core, 1, 0x1234});
  m.call("__sceSasSetPitch", {Core, 0, 0x0c00});
  for(u32 voice : {0u, 1u}) m.call("__sceSasSetKeyOn", {Core, voice});
  CHECK(m.call("__sceSasCore", {Core, Out}), 0);
  auto state = saveState(m);
  KernelMachine n;
  CHECK(loadState(n, state), true);
  CHECK(saveState(n) == state, true);
  for(u32 grain = 0; grain < 3; grain++) {
    CHECK(m.call("__sceSasCore", {Core, Out}), 0);
    CHECK(n.call("__sceSasCore", {Core, Out}), 0);
    std::vector<u8> first(192 * 4), second(192 * 4);
    m.system.memory.copyOut(first.data(), Out, 192 * 4);
    n.system.memory.copyOut(second.data(), Out, 192 * 4);
    CHECK(first == second, true);
    CHECK(std::any_of(first.begin(), first.end(), [](u8 byte) { return byte; }), true);
  }
}

auto sasTests() -> Tests {
  return {{"sas settings", sasSettings}, {"sas envelopes", sasEnvelopes}, {"sas voices end", sasVoicesEnd},
          {"sas voices given new samples", sasNewSamples}, {"sas vag as recorded", sasVagRecorded},
          {"sas vag decoded", sasVagDecoded}, {"sas pcm heard", sasPcmHeard}, {"sas output modes", sasOutputModes},
          {"sas voices mixed", sasMixed}, {"sas voices in states", sasVoicesInStates}};
}

}
