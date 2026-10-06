//sceSasCore (ares/psp/kernel/sas.cpp), silent: what its functions take and refuse, as pspautotests' audio/sascore
//recorded on a PSP; envelopes grain by grain against the same recordings; voices ending as their samples run out or
//their release comes down to 0, and the end flags refreshed by __sceSasCore; in a program on both engines too.
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
  //the buffer: a grain of silence, stereo; mixing leaves it as it was
  m.system.memory.fill(Out, 0x55, 64 * 4 + 4);
  core();
  CHECK(m.system.memory.read(4, Out) == 0 && m.system.memory.read(4, Out + 63 * 4) == 0, true);
  CHECK(m.system.memory.read(4, Out + 64 * 4), 0x5555'5555);
  m.system.memory.fill(Out, 0x55, 64 * 4);
  CHECK(m.call("__sceSasCoreWithMix", {Core, Out, 0x1000, 0x1000}), 0);
  CHECK(m.system.memory.read(4, Out), 0x5555'5555);
  CHECK(m.call("__sceSasCore", {Core, 0}), ErrorAddress);
  CHECK(m.notes.size(), 0);
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
  }
}

auto sasTests() -> Tests {
  return {{"sas settings", sasSettings}, {"sas envelopes", sasEnvelopes}, {"sas voices end", sasVoicesEnd}};
}

}
