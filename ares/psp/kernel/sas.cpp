//sceSasCore: the software synthesizer games' sound libraries play through (the PSP runs it on its second CPU, the
//Media Engine). A game sets up to 32 voices, each playing samples (VAG ADPCM blocks, 16-bit PCM, or noise and simple
//waves) at a pitch, shaped by an ADSR envelope, keys them on and off, and calls __sceSasCore once per grain (64 to
//2048 samples at 44.1 kHz) to have them mixed into a buffer, which it hands to sceAudio. Its sound thread watches the
//voices' end flags to know which have finished.
//
//This one is silent: the buffer __sceSasCore fills is silence (and __sceSasCoreWithMix leaves its buffer as it was),
//but everything a game can see is kept as it would be: each voice's parameters, its envelope's height, where it is in
//its samples, and its end flag, set as it finishes (its VAG data reaching an end mark or its last byte, its PCM its
//last sample, or its release coming down to 0). So a sound thread waiting for voices to end, or choosing free ones,
//runs as it would. Mixing the samples comes later.
//
//What each call accepts and refuses is pspsdk's pspsascore.h (the functions, their arguments, the error numbers) and
//pspautotests' audio/sascore tests, whose results were recorded on a PSP: __sceSasInit's checks and their order, the
//volumes, pitches, sizes and loop values taken, which curve each envelope phase takes, keying on and off, pausing,
//and the envelope's progress grain by grain. From those recordings:
//- A voice keyed on starts 32 samples into the next grain: at a grain of 128 and an attack rate of 0x1000 its first
//  __sceSasCore raises it by 96 * 0x1000 (keyon), then by 128 * 0x1000 each (getheight, keyoff).
//- The envelope moves once a sample, through its phases: attack up to 0x40000000, decay down to the sustain level,
//  sustain until the voice is keyed off, release down to 0, when the voice ends. A phase ends on the sample it
//  reaches its target, the next going on from the following sample (adsrcurve: an attack of 0x1000000 at a grain of
//  64 reaches the top 32 samples into its second grain and decays by 32 at rate 1 to 0x3fffffe0).
//- The curves (pspsdk's names): linear increase adds the rate, linear decrease takes it away, direct sets the height
//  to it; exponent rev takes away height * rate / 2^32 a sample, rounded up, so it always reaches 0 (adsrcurve's
//  decay: 0x40000000 to 0x3f01f557, 0x3e07db13, 0x3d11a192 grain by grain at 0x100000, and 1 a sample at rates 1-4,
//  2 at 5-8, exactly); linear bent adds the rate up to three quarters of the top (that reached, once more) and a
//  quarter of it above (adsrcurve's 0x100000 bends to 0x308c0000, then reaches the top at 0x3fffffdd, exactly).
//  Exponent, which only the attack and sustain take, adds 0x4000 plus a quarter of the rate scaled by what's left to
//  the top: an approximation of adsrcurve's figures, close to them but not exact.
//- __sceSasSetSimpleADSR's two words, a PlayStation SPU's ADSR registers, give rates by the formulas worked out from
//  setadsr's table (see sasRate()).
//- The end flags are refreshed only by __sceSasCore (pspsdk's note); a voice keyed on twice, or keyed off when it
//  isn't on, or either while it's paused, is refused (INVALID_STATE).
//Not known, and chosen as games accept it: how VAG's end marks are read beyond what vag's blocks show (flags 1, 7,
//0x41 and 0x87 end the voice, 3 loops when the voice loops, 4 marks where the loop goes back to, 0, 2 and 6 go on);
//that a voice keyed on with no samples set ends at the next __sceSasCore; and that every call works on the one
//SasCore __sceSasInit set up, whatever core it's given. Nothing time-consuming is done: __sceSasCore returns at once,
//where a PSP's waits for the Media Engine.

namespace {
  //pspsdk's pspsascore.h; and __sceSasInit's own four, which pspautotests' audio/sascore recorded: a bad grain, voice
  //count, output mode or sample rate. A PCM voice's bad sample count is audio/sascore/pcm's.
  constexpr u32 SasErrorGrain = 0x8042'0001, SasErrorVoices = 0x8042'0002, SasErrorMode = 0x8042'0003;
  constexpr u32 SasErrorSampleRate = 0x8042'0004, SasErrorAddress = 0x8042'0005, SasErrorVoice = 0x8042'0010;
  constexpr u32 SasErrorNoiseClock = 0x8042'0011, SasErrorPitch = 0x8042'0012, SasErrorCurve = 0x8042'0013;
  constexpr u32 SasErrorSize = 0x8042'0014, SasErrorLoop = 0x8042'0015, SasErrorState = 0x8042'0016;
  constexpr u32 SasErrorVolume = 0x8042'0018, SasErrorRate = 0x8042'0019, SasErrorPcmSize = 0x8042'001a;
  constexpr u32 SasErrorEffectType = 0x8042'0020, SasErrorFeedback = 0x8042'0021, SasErrorDelay = 0x8042'0022;
  constexpr u32 SasErrorEffectVolume = 0x8042'0023, SasErrorNotInitialized = 0x8042'0100;
  constexpr s64 EnvelopeTop = 0x4000'0000;
  constexpr u32 KeyOnDelay = 32;  //samples a voice keyed on waits in its first grain
  enum : u32 { Increase = 0, Decrease = 1, Bent = 2, ExponentRev = 3, Exponent = 4, Direct = 5 };

  //A grain: 64 to 2048 samples, in 64s.
  auto sasGrainValid(u32 grain) -> bool { return grain >= 0x40 && grain <= 0x800 && grain % 0x40 == 0; }

  //A rate from an SPU's 7-bit rate (attack, sustain): a step of 7 down to 4 (by the rate's low two bits) shifted by a
  //place for every 4 of the rest, from 26 places left (24 for an exponential decrease's, which steps a quarter as
  //far), at least 1; 0x7f never moves. So 0 gives 0x1c000000, 1 0x18000000, 0x60 0x1c, 0x6c 3, 0x70 1 (setadsr).
  auto sasRate(u32 rate, u32 places) -> s32 {
    if(rate == 0x7f) return 0;
    u32 shift = rate >> 2, step = 7 - (rate & 3);
    u32 value = shift <= places ? step << (places - shift) : step >> std::min(shift - places, 31u);
    return std::max(1u, value);
  }
}

//The voice a call names (0-31); null, with VOICE_INDEX for the result, for any other.
auto Kernel::sasVoice(u32 number) -> Sas::Voice* {
  if(number >= 32) return result(SasErrorVoice), nullptr;
  return &sas.voices[number];
}

//One sample of a voice's envelope: its phase's curve moves its height (kept within 0 and the top), and the phase
//ends when it reaches its target. A release coming down to 0 ends the voice.
auto Kernel::sasEnvelope(Sas::Voice& voice) -> void {
  u32 index = u32(voice.phase);
  s64 height = voice.height, rate = voice.rates[index];
  switch(voice.curves[index]) {
  case Increase:    height += rate; break;
  case Decrease:    height -= rate; break;
  case Bent:        height += height <= EnvelopeTop * 3 / 4 ? rate : rate / 4; break;
  case ExponentRev: height -= (height * rate + 0xffff'ffffll) >> 32; break;
  case Exponent:    height += 0x4000 + rate / 4 * (EnvelopeTop - height) / EnvelopeTop; break;
  case Direct:      height = rate; break;
  }
  voice.height = s32(std::clamp<s64>(height, 0, EnvelopeTop));
  switch(voice.phase) {
  case Sas::Phase::Attack:
    if(voice.height >= EnvelopeTop) voice.phase = Sas::Phase::Decay;
    break;
  case Sas::Phase::Decay:
    if(voice.height <= voice.sustainLevel) voice.phase = Sas::Phase::Sustain;
    break;
  case Sas::Phase::Sustain:
    break;
  case Sas::Phase::Release:
    if(voice.height <= 0) voice.playing = voice.on = false;
    break;
  }
}

//A voice moves on through its samples, so many made at its pitch, and ends if they run out: a PCM voice at its last
//sample unless it loops; a VAG voice at the end of a block marked as the end (or as a loop's, when it doesn't loop),
//or at its data's last byte. A VAG block is 16 bytes making 28 samples: a byte of filter and shift, a byte of flags,
//then the samples' 4-bit codes; the flags are read as the voice gets to each block, so data a game writes ahead of a
//voice (streaming) counts.
auto Kernel::sasAdvance(Sas::Voice& voice, u32 samples) -> void {
  u64 to = voice.position + u64(samples) * voice.pitch;
  if(voice.source == Sas::Source::None) {  //nothing to play
    voice.playing = voice.on = false;
    return;
  }
  if(voice.source == Sas::Source::Pcm) {
    u64 end = u64(voice.size) << 12;
    if(to >= end) {
      if(voice.loop < 0) return void(voice.playing = voice.on = false);
      u64 start = u64(voice.loop) << 12;
      to = start + (to - end) % (end - start);
    }
    voice.position = to;
    return;
  }
  if(voice.source != Sas::Source::Vag) {  //noise and waves go on until keyed off
    voice.position = to;
    return;
  }
  u64 sample = voice.position >> 12, target = to >> 12;
  while(true) {
    u64 block = sample / 28, blockEnd = (block + 1) * 28;
    if(block * 16 >= voice.size || !memory.reaches(voice.address + u32(block * 16), 16)) {
      return void(voice.playing = voice.on = false);
    }
    u32 flags = memory.read(1, voice.address + u32(block * 16) + 1) & 7;
    if(flags == 4 || flags == 6) voice.loopBlock = u32(block);  //where a loop goes back to
    if(target < blockEnd) break;
    if(flags & 1) {  //the end of the data, or of its loop
      if(flags != 3 || !voice.loop) return void(voice.playing = voice.on = false);
      sample = u64(voice.loopBlock) * 28;
      target = sample + (target - blockEnd);
      continue;
    }
    sample = blockEnd;
  }
  voice.position = target << 12 | (to & 0xfff);
}

//(core, grain, voices, output mode, sample rate): the synthesizer set up afresh in a SasCore (64-byte aligned), every
//voice as it starts: no samples, ended. Checked in this order, as pspautotests' audio/sascore found: the core (null
//or unaligned: ADDRESS), the grain (64 to 2048 in 64s), the voices (1 to 32), the output mode (stereo 0 or
//multichannel 1), the rate (44100 alone). Setting it up again is allowed.
auto Kernel::__sceSasInit() -> void {
  u32 core = arg(0), grain = arg(1), voices = arg(2), mode = arg(3), rate = arg(4);
  if(!core || core & 63) return result(SasErrorAddress);
  if(!sasGrainValid(grain)) return result(SasErrorGrain);
  if(voices < 1 || voices > 32) return result(SasErrorVoices);
  if(mode > 1) return result(SasErrorMode);
  if(rate != 44'100) return result(SasErrorSampleRate);
  sas = {};
  sas.initialized = true;
  sas.core = core;
  sas.grain = grain;
  sas.voiceCount = voices;
  sas.outputMode = mode;
  result(0);
}

//A grain made: every playing voice that isn't paused moves on by the grain's samples (a voice keyed on waiting its
//first 32 first), its envelope a sample at a time, and the end flags are refreshed. The buffer gets silence, a grain
//of stereo pairs (stereo) or of four 16-bit channels (multichannel); mixing leaves it as it was.
auto Kernel::sasMix(bool mix) -> void {
  u32 buffer = arg(1);
  if(!sas.initialized) return result(SasErrorNotInitialized);
  u32 bytes = sas.grain * (sas.outputMode ? 8 : 4);
  if(!buffer || !memory.reaches(buffer, bytes)) return result(SasErrorAddress);
  if(!mix) memory.fill(buffer, 0, bytes);
  for(u32 number = 0; number < 32; number++) {
    auto& voice = sas.voices[number];
    if(!voice.playing || (sas.paused >> number & 1)) continue;
    u32 samples = sas.grain, waits = std::min(voice.delay, samples);
    voice.delay -= waits;
    samples -= waits;
    for(u32 sample = 0; sample < samples && voice.playing; sample++) sasEnvelope(voice);
    if(voice.playing) sasAdvance(voice, samples);
  }
  sas.endFlags = 0;
  for(u32 number = 0; number < 32; number++) if(!sas.voices[number].playing) sas.endFlags |= 1u << number;
  result(0);
}

//(core, buffer)
auto Kernel::__sceSasCore() -> void {
  sasMix(false);
}

//(core, buffer, left volume, right volume): the voices mixed into what's in the buffer (silence: it stays as it is)
auto Kernel::__sceSasCoreWithMix() -> void {
  sasMix(true);
}

//(core): the voices ended, a bit each, as the last __sceSasCore left them (every voice, before the first).
auto Kernel::__sceSasGetEndFlag() -> void {
  result(sas.endFlags);
}

//(core, voice, left, right, effect's left, effect's right): each -0x1000 to 0x1000, else VOLUME_VAL.
auto Kernel::__sceSasSetVolume() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  s32 volumes[4] = {s32(arg(2)), s32(arg(3)), s32(arg(4)), s32(arg(5))};
  for(s32 volume : volumes) if(volume < -0x1000 || volume > 0x1000) return result(SasErrorVolume);
  for(u32 n = 0; n < 4; n++) voice->volumes[n] = volumes[n];
  result(0);
}

//(core, voice, pitch): 0 to 0x4000 (4096ths), else PITCH_VAL.
auto Kernel::__sceSasSetPitch() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  if(arg(2) > 0x4000) return result(SasErrorPitch);
  voice->pitch = arg(2);
  result(0);
}

//(core, voice, VAG data, its size, loop): the voice plays VAG ADPCM blocks. The size a multiple of 16, not 0 (a
//negative one is taken, as audio/sascore/vag found: the data then runs until its end mark); loop 0 or 1. A voice
//playing VAG already goes on from where it is, in the new data (past its end, it ends as it next moves on); one that
//played other samples starts from the new data's beginning, its loop too, as where it was means nothing in them.
auto Kernel::__sceSasSetVoice() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  s32 size = s32(arg(3)), loop = s32(arg(4));
  if(!size || size & 15) return result(SasErrorSize);
  if(loop != 0 && loop != 1) return result(SasErrorLoop);
  if(voice->source != Sas::Source::Vag) voice->position = 0, voice->loopBlock = 0;
  voice->source = Sas::Source::Vag;
  voice->address = arg(2);
  voice->size = u32(size);
  voice->loop = loop;
  result(0);
}

//(core, voice, PCM data, samples, loop start): 16-bit mono samples, 1 to 65536 of them, looping from the start given
//(below the count; a negative one plays once). A voice playing PCM already goes on from where it is, in the new
//samples; one that played others starts from their beginning. Where it is must be inside them: past their end, a
//voice that loops goes on from where its loop would have taken it, and one that doesn't from its last sample, so
//that it ends as it next moves on, as it would have (a state of it then loads).
auto Kernel::__sceSasSetVoicePCM() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  s32 samples = s32(arg(3)), loop = s32(arg(4));
  if(samples <= 0 || samples > 0x10000) return result(SasErrorPcmSize);
  if(loop >= samples) return result(SasErrorLoop);
  if(voice->source != Sas::Source::Pcm) voice->position = 0;
  voice->source = Sas::Source::Pcm;
  voice->address = arg(2);
  voice->size = u32(samples);
  voice->loop = loop < 0 ? -1 : loop;
  voice->loopBlock = 0;  //(VAG's)
  u64 end = u64(voice->size) << 12;
  if(voice->position >= end && voice->loop < 0) voice->position = end - 1;
  if(voice->position >= end) {
    u64 start = u64(voice->loop) << 12;
    voice->position = start + (voice->position - end) % (end - start);
  }
  result(0);
}

//(core, voice, frequency 0-0x3f): the voice plays noise, until it's keyed off.
auto Kernel::__sceSasSetNoise() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  if(arg(2) > 0x3f) return result(SasErrorNoiseClock);
  voice->source = Sas::Source::Noise;
  result(0);
}

//(core, voice, shape): a triangle or a square wave, until it's keyed off.
auto Kernel::__sceSasSetTrianglarWave() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  voice->source = Sas::Source::Triangle;
  result(0);
}

auto Kernel::__sceSasSetSteepWave() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  voice->source = Sas::Source::Steep;
  result(0);
}

//(core, voice, ATRAC3 context): the voice plays an ATRAC3 stream fed to it (__sceSasConcatenateATRAC3), until it's
//keyed off; unset, it has no samples.
auto Kernel::__sceSasSetVoiceATRAC3() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  voice->source = Sas::Source::Atrac;
  voice->address = arg(2);
  result(0);
}

auto Kernel::__sceSasConcatenateATRAC3() -> void {
  if(!sasVoice(arg(1))) return;
  result(0);
}

auto Kernel::__sceSasUnsetATRAC3() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  voice->source = Sas::Source::None;
  result(0);
}

//(core, voice, which (a bit for each of attack, decay, sustain, release), and the four rates): those chosen set,
//each 0 or more, else ADSR_VAL and none set.
auto Kernel::__sceSasSetADSR() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  u32 which = arg(2);
  s32 rates[4] = {s32(arg(3)), s32(arg(4)), s32(arg(5)), s32(arg(6))};
  for(u32 n = 0; n < 4; n++) if(which >> n & 1 && rates[n] < 0) return result(SasErrorRate);
  for(u32 n = 0; n < 4; n++) if(which >> n & 1) voice->rates[n] = rates[n];
  result(0);
}

//(core, voice, which, and the four curves): those chosen set. A curve is 0-5 (its top bit ignored, as
//audio/sascore/setadsr found); the attack takes only the rising ones (linear increase, linear bent, exponent), the
//decay and release only the falling ones (linear decrease, exponent rev, direct), the sustain any. Else ADSR_MODE,
//and none set.
auto Kernel::__sceSasSetADSRmode() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  u32 which = arg(2), curves[4];
  for(u32 n = 0; n < 4; n++) {
    curves[n] = arg(3 + n) & 0x7fff'ffff;
    if(!(which >> n & 1)) continue;
    bool rising = curves[n] == Increase || curves[n] == Bent || curves[n] == Exponent;
    bool valid = curves[n] <= 5 && (n == 2 || (n == 0 ? rising : !rising));
    if(!valid) return result(SasErrorCurve);
  }
  for(u32 n = 0; n < 4; n++) if(which >> n & 1) voice->curves[n] = curves[n];
  result(0);
}

//(core, voice, first word, second word): the whole envelope from a PlayStation SPU's two ADSR words, as
//audio/sascore/setadsr's table gives it. The first: the sustain level in bits 0-3 ((level + 1) * 0x4000000), the
//decay rate in 4-7 (0x80000000 >> rate, the top for 0; exponent rev), the attack rate in 8-14 (sasRate()), and bit
//15 a bent attack rather than a linear one. The second: the release rate in bits 0-4 and bit 5 an exponent rev
//release rather than a linear one (linear: 1 << (28 - rate) as the PSP's 5-bit shift takes it, 1 if that's
//negative; exponential: 0x80000000 >> rate, the top for 0; 0x1f never moves either way), the sustain rate in 6-12
//and its curve in 14-15 (linear increase, linear decrease, bent, exponent rev, which steps a quarter as far:
//sasRate(rate, 24)). Bit 13 isn't one: set, the call is refused (ADSR_MODE).
auto Kernel::__sceSasSetSimpleADSR() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  u32 first = arg(2), second = arg(3);
  if(second & 0x2000) return result(SasErrorCurve);
  auto halving = [](u32 rate) -> s32 { return rate ? s32(0x8000'0000u >> rate) : 0x7fff'ffff; };
  voice->sustainLevel = s32(((first & 15) + 1) * 0x0400'0000);
  voice->rates[1] = halving(first >> 4 & 15);
  voice->curves[1] = ExponentRev;
  voice->rates[0] = sasRate(first >> 8 & 0x7f, 26);
  voice->curves[0] = first & 0x8000 ? Bent : Increase;
  u32 release = second & 0x1f;
  if(second & 0x20) {
    voice->rates[3] = release == 0x1f ? 0 : halving(release);
    voice->curves[3] = ExponentRev;
  } else {
    s32 linear = s32(1u << ((28 - release) & 31));
    voice->rates[3] = release == 0x1f ? 0 : std::max(linear, 1);
    voice->curves[3] = Decrease;
  }
  u32 sustain = second >> 6 & 0x7f, curve = second >> 14 & 3;
  voice->rates[2] = sasRate(sustain, curve == ExponentRev ? 24 : 26);
  voice->curves[2] = curve;
  result(0);
}

//(core, voice, level): where the decay ends and the sustain begins.
auto Kernel::__sceSasSetSL() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  voice->sustainLevel = s32(arg(2));
  result(0);
}

//(core, voice): its envelope's height, 0 to 0x40000000.
auto Kernel::__sceSasGetEnvelopeHeight() -> void {
  auto voice = sasVoice(arg(1));
  if(!voice) return;
  result(voice->height);
}

//(core, where to put 32 heights)
auto Kernel::__sceSasGetAllEnvelopeHeights() -> void {
  u32 heights = arg(1);
  if(!memory.reaches(heights, 32 * 4)) return result(SasErrorAddress);
  for(u32 n = 0; n < 32; n++) memory.write(4, heights + n * 4, u32(sas.voices[n].height));
  result(0);
}

//(core, voice): the voice starts from its samples' beginning, its envelope from 0 in its attack, 32 samples into
//the next grain. Refused (INVALID_STATE) while it's on already, or paused.
auto Kernel::__sceSasSetKeyOn() -> void {
  u32 number = arg(1);
  auto voice = sasVoice(number);
  if(!voice) return;
  if(voice->on || sas.paused >> number & 1) return result(SasErrorState);
  voice->on = voice->playing = true;
  voice->phase = Sas::Phase::Attack;
  voice->height = 0;
  voice->delay = KeyOnDelay;
  voice->position = 0;
  voice->loopBlock = 0;
  result(0);
}

//(core, voice): its envelope goes into its release, and the voice ends when that reaches 0. Refused
//(INVALID_STATE) unless it's on, and while it's paused.
auto Kernel::__sceSasSetKeyOff() -> void {
  u32 number = arg(1);
  auto voice = sasVoice(number);
  if(!voice) return;
  if(!voice->on || sas.paused >> number & 1) return result(SasErrorState);
  voice->on = false;
  voice->phase = Sas::Phase::Release;
  result(0);
}

//(core, voices (a bit each), pause): paused (any pause but 0) or let go. A paused voice stands still: it doesn't
//move on, nor its envelope.
auto Kernel::__sceSasSetPause() -> void {
  if(arg(2)) sas.paused |= arg(1);
  else sas.paused &= ~arg(1);
  result(0);
}

auto Kernel::__sceSasGetPauseFlag() -> void {
  result(sas.paused);
}

auto Kernel::__sceSasGetGrain() -> void {
  result(sas.grain);
}

//(core, grain): as __sceSasInit takes it.
auto Kernel::__sceSasSetGrain() -> void {
  if(!sasGrainValid(arg(1))) return result(SasErrorGrain);
  sas.grain = arg(1);
  result(0);
}

auto Kernel::__sceSasGetOutputmode() -> void {
  result(sas.outputMode);
}

//(core, mode): stereo (0) or multichannel (1), as audio/sascore/outputmode found.
auto Kernel::__sceSasSetOutputmode() -> void {
  if(arg(1) > 1) return result(SasErrorMode);
  sas.outputMode = arg(1);
  result(0);
}

//The effect (reverb), kept for when the voices are mixed. (core, type): -1 (off) to 8 (pspsdk's types).
auto Kernel::__sceSasRevType() -> void {
  s32 type = s32(arg(1));
  if(type < -1 || type > 8) return result(SasErrorEffectType);
  sas.effectType = type;
  result(0);
}

//(core, delay, feedback): each 0 to 128.
auto Kernel::__sceSasRevParam() -> void {
  if(arg(1) > 128) return result(SasErrorDelay);
  if(arg(2) > 128) return result(SasErrorFeedback);
  sas.effectDelay = arg(1);
  sas.effectFeedback = arg(2);
  result(0);
}

//(core, left, right): the effect's volumes, each 0 to 0x1000.
auto Kernel::__sceSasRevEVOL() -> void {
  if(arg(1) > 0x1000 || arg(2) > 0x1000) return result(SasErrorEffectVolume);
  sas.effectLeft = arg(1);
  sas.effectRight = arg(2);
  result(0);
}

//(core, dry, wet): whether the voices are heard as they are, and through the effect.
auto Kernel::__sceSasRevVON() -> void {
  sas.effectDry = arg(1) != 0;
  sas.effectWet = arg(2) != 0;
  result(0);
}
