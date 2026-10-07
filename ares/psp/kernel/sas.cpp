//sceSasCore: the software synthesizer games' sound libraries play through (the PSP runs it on its second CPU, the
//Media Engine). A game sets up to 32 voices, each playing samples (VAG ADPCM blocks, 16-bit PCM, or noise and simple
//waves) at a pitch, shaped by an ADSR envelope, keys them on and off, and calls __sceSasCore once per grain (64 to
//2048 samples at 44.1 kHz) to have them mixed into a buffer, which it hands to sceAudio. Its sound thread watches the
//voices' end flags to know which have finished.
//
//How a grain is made (sasGrain()), sample by sample, for each voice that's playing and not paused:
//- Its sample, where it is. A PCM voice's samples are 16-bit numbers in memory, read as they're needed. A VAG voice's
//  come packed in 16-byte blocks of 28 samples each, 4 bits a sample instead of 16: ADPCM, the PlayStation SPU's
//  format (psx-spx describes it). Four bits can't hold a sample, so they hold a correction: the decoder guesses each
//  sample from the two before it, and the 4 bits say how far off the guess is (vagDecode()). A block's first byte
//  says how to guess (its filter: one of five ways, from "no guess" to "carry on the way the last two went") and how
//  big the corrections are (its shift: big ones for loud passages, small for quiet); its second byte holds flags
//  (where a loop starts, where the data or its loop ends); the other 14 bytes hold the 28 corrections, low 4 bits
//  first. As a VAG voice moves on, each sample is decoded in turn after the two before it (decoded[]), across blocks
//  and round its loop.
//- Its pitch: 0x1000 moves on a sample for every sample made (its samples play at 44.1 kHz), 0x2000 two (an octave
//  up), 0x800 half of one. Where it is counts in 4096ths of a sample; between two samples, the sample made is the
//  straight line between them, weighed by how near it is to each (linear interpolation).
//- Its envelope: a height from 0 to 0x40000000 that the sample is multiplied by (over 0x40000000), as it is before
//  this sample moves it on (sasEnvelope()). So the first sample after a voice is keyed on is silent, the height
//  being 0 there.
//- Its volumes, -0x1000 to 0x1000 each (0x1000 as it is, a negative one turning the wave upside down): left and
//  right give its "dry" sound, the effect's left and right what it sends to the effect, its "wet" sound. Every
//  voice's dry and wet samples are added up.
//Then the grain is written, each sum clamped to 16 bits (-32768 to 32767). In stereo (output mode 0) as left and
//right pairs: the dry sums if the voices are heard dry (__sceSasRevVON's first flag, on from __sceSasInit), plus,
//if they're heard through the effect (its second flag, off until then) and an effect is chosen, the wet sums at the
//effect's volumes (__sceSasRevEVOL, 0 until set). The effect itself isn't made: no reverb, echo or delay is added,
//the wet sums pass through it as they are. In multichannel (mode 1) as four planes of a grain each: the dry left,
//dry right, wet left and wet right sums, for the game to mix itself. __sceSasCoreWithMix adds the voices to what
//the buffer holds, that scaled first by its own left and right volumes (over 0x1000).
//
//What the recordings show (pspautotests' audio/sascore, run on a PSP), which this follows exactly:
//- vag: VAG's guess rounds down, with no half added as psx-spx's SPU adds one (music.vag's samples 0x2d4b and 0x2cb4,
//  exactly); filters 5-15 read on past the PSP's table of five (VagFilters); each VAG sample is heard a sample late,
//  after the one before it (silence first, then the first sample decoded), where a PCM voice's is heard at its place
//  (pcm: its sample 254 as the 254th made); the end marks are whole bytes (music.vag's header, read as blocks, has
//  flags 0x41 and 0x75 and plays on).
//- outputmode: volumes 0x1000, 0xC00, 0x800, 0x400 give a sample whole, three quarters, half and a quarter of it,
//  rounded down (-3 becomes -3 at 0xC00); multichannel's four planes; __sceSasCoreWithMix's volumes (0, 0) leave the
//  voices alone; and __sceSasCoreWithMix refusing multichannel (0x80000004), the buffer untouched.
//- pcm, outputmode: dry is heard without __sceSasRevVON (pspautotests' sascore.h found the flags 1, dry, in a fresh
//  SasCore, and the effect's volumes 0).
//Chosen, as no recording shows them: the straight line between samples at other pitches; the envelope's and the
//volumes' rounding at heights and volumes between (each a multiplication rounded down); shifts 13-15 as 9, as psx-spx
//says the SPU takes them; paused voices silent; noise, triangle and steep waves silent (their parameters kept, as
//before), ATRAC3 voices silent (no decoder yet); and the effect passing the wet sound through as it is, where a PSP
//adds its reverb or echo.
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
//Not known, and chosen as games accept it: how VAG's end marks are read beyond what vag's blocks show (a block whose
//flags byte is 1 or 7 ends the voice, 3 loops when the voice loops and ends it when it doesn't, 4 and 6 mark where
//the loop goes back to, and any other byte goes on, 0x41 and 0x75 among them as recorded, 0x87 too); that a voice
//keyed on with no samples set ends at the next __sceSasCore; and that every call works on the one SasCore
//__sceSasInit set up, whatever core it's given. Nothing time-consuming is done: __sceSasCore returns at once, where
//a PSP's waits for the Media Engine (but where a thread can't wait, it's refused as a PSP's is: sasMix()).

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

  //VAG's filters: how the decoder guesses a sample from the two before it, the last one times the filter's first
  //coefficient less the one before times its second, over 64. Filter f's coefficients are VagFilters[f] and
  //VagFilters[f + 5]: the SPU's five, (0, 0) (no guess), (60, 0), (115, 52), (98, 55) and (122, 60) (psx-spx), take
  //the first ten; filters 5-15 read on past them, into what follows the PSP's table, and the rest is that, as
  //audio/sascore/vag's predict_nr 5-15 recorded it (filter 7 guesses with 52 and 0, 9 with 60 and 125, 13 with 2 and
  //216...). Only VagFilters[19], filter 14's second coefficient, didn't show (any up to 85 fits): 0 here.
  constexpr s32 VagFilters[21] = {0, 60, 115, 98, 122, 0, 0, 52, 55, 60, 0, 0, 0, 2, 125, 0, 91, 0, 216, 0, 151};

  //One VAG sample: the index-th (0-27) of a block (its 16 bytes), after the two decoded before it (older, then old).
  //Its 4 bits, -8 to 7, become a correction: times 4096 (a 16-bit sample's top 4 bits), shifted right by the
  //block's shift, so a shift of 0 makes the biggest corrections and 12 the smallest (13-15 act as 9, as psx-spx says
  //of the SPU). The guess from the two before it is added (rounded down: shifted right by 6), and the sample kept
  //within 16 bits.
  auto vagDecode(const u8* block, u32 index, s32 older, s32 old) -> s16 {
    u32 shift = block[0] & 15, filter = block[0] >> 4;
    if(shift > 12) shift = 9;
    s32 bits = block[2 + index / 2] >> (index & 1) * 4 & 15;
    s32 correction = (bits < 8 ? bits : bits - 16) * 4096 >> shift;
    s32 guess = (old * VagFilters[filter] - older * VagFilters[filter + 5]) >> 6;
    return s16(std::clamp(correction + guess, -32768, 32767));
  }

  //A sample made between two (first, then second), weight 4096ths of the way from the first to the second.
  auto sasBetween(s32 first, s32 second, s32 weight) -> s32 {
    return first + ((second - first) * weight >> 12);
  }

  //The 16-bit PCM sample at address, or silence where there's no memory.
  auto pcmSample(Memory& memory, u32 address) -> s32 {
    auto bytes = memory.pointer(address, 2);
    return bytes ? s16(bytes[0] | bytes[1] << 8) : 0;
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

//A VAG voice gets to a block of its data: false if the data has run out there (the block starts past its size, or
//there's no memory behind it), else its 16 bytes go in bytes, and a block marked as where the loop starts (flags 4
//or 6) is noted as that. The flags are read as the voice gets to each block, so data a game writes ahead of a voice
//(streaming) counts.
auto Kernel::sasBlock(Sas::Voice& voice, u32 block, u8* bytes) -> bool {
  if(u64(block) * 16 >= voice.size || !memory.copyOut(bytes, voice.address + block * 16, 16)) return false;
  if(bytes[1] == 4 || bytes[1] == 6) voice.loopBlock = block;
  return true;
}

//A VAG voice moves on to its next sample, decoded after the two before it: the next in its block, or, past a block's
//end, the next block's first; but the end of a block marked as the end (flags 1 or 7, or 3 when the voice doesn't
//loop) ends the voice, and one marked as a loop's end (3) takes it back to the loop's block. bytes holds the block
//it's in, and the one it gets to after. False if the samples run out.
auto Kernel::sasNext(Sas::Voice& voice, u8* bytes) -> bool {
  u64 sample = (voice.position >> 12) + 1;
  if(sample % 28 == 0) {
    u8 flags = bytes[1];
    if(flags == 1 || flags == 7 || (flags == 3 && !voice.loop)) return false;
    if(flags == 3) sample = u64(voice.loopBlock) * 28;
    if(sample / 28 > 0xffff'ffff || !sasBlock(voice, u32(sample / 28), bytes)) return false;
  }
  s16 next = vagDecode(bytes, sample % 28, voice.decoded[0], voice.decoded[1]);
  voice.decoded[0] = voice.decoded[1];
  voice.decoded[1] = next;
  voice.position = sample << 12 | (voice.position & 0xfff);
  return true;
}

//The sample a voice makes where it is, between its sample there and the next. A PCM voice's: those two from memory
//(past its last, its loop's first, or silence). A VAG voice's lag a sample: the two decoded before its place and at
//it. Noise, waves and ATRAC3: silence.
auto Kernel::sasSample(const Sas::Voice& voice) -> s32 {
  s32 weight = voice.position & 0xfff;
  if(voice.source == Sas::Source::Vag) return sasBetween(voice.decoded[0], voice.decoded[1], weight);
  if(voice.source != Sas::Source::Pcm) return 0;
  u64 sample = voice.position >> 12;
  s32 here = pcmSample(memory, voice.address + u32(sample) * 2), next = 0;
  if(sample + 1 < voice.size) next = pcmSample(memory, voice.address + u32(sample + 1) * 2);
  else if(voice.loop >= 0) next = pcmSample(memory, voice.address + u32(voice.loop) * 2);
  return weight ? sasBetween(here, next, weight) : here;
}

//A voice's part of a grain, from sample `from` (after the 32 a voice keyed on waits) to its end. At each sample: its
//sample where it is, under its envelope as it is, at each of its four volumes, is added to the sums (four planes of
//a grain: dry left, dry right, wet left, wet right; those the grain won't be written from are left out, as they'd
//be thrown away); then its envelope moves on (a release coming down to 0 ends the voice there), and the voice moves
//on by its pitch, a sample at a time. Its samples running out end it: a PCM voice past its last sample unless it
//loops (to its loop's start), a VAG voice at an end mark or its data's end (each grain looks at the block it's in
//afresh: data given to it meanwhile may end there), a voice with nothing set at once. Noise and waves go on until
//keyed off. A voice that ends stays where it was as the grain began, as nothing more of it is heard; its envelope
//still moves on to the grain's end, unless its release has ended it.
auto Kernel::sasGrain(Sas::Voice& voice, u32 from, s32* sums) -> void {
  u32 grain = sas.grain;
  u64 begin = voice.position;
  u8 bytes[16];
  bool ended = voice.source == Sas::Source::None;
  if(voice.source == Sas::Source::Vag) {
    ended = (voice.position >> 12) / 28 > 0xffff'ffff || !sasBlock(voice, u32((voice.position >> 12) / 28), bytes);
    if(!ended && !voice.started) {  //its first sample, decoded after silence
      s16 here = vagDecode(bytes, (voice.position >> 12) % 28, voice.decoded[0], voice.decoded[1]);
      voice.decoded[0] = voice.decoded[1];
      voice.decoded[1] = here;
      voice.started = true;
    }
  }
  //the planes the grain will be written from: the dry pair if the voices are heard dry, the wet pair if they're
  //heard through the effect (in stereo, only if one is chosen)
  u32 first = sas.effectDry ? 0 : 2;
  u32 last = sas.effectWet && (sas.outputMode || sas.effectType >= 0) ? 4 : 2;
  s64 volumes[4] = {voice.volumes[0], voice.volumes[1], voice.volumes[2], voice.volumes[3]};
  for(u32 n = from; n < grain && voice.playing; n++) {
    if(!ended && first < last) {
      s64 sample = s64(sasSample(voice)) * voice.height >> 30;
      for(u32 plane = first; plane < last; plane++) sums[plane * grain + n] += s32(sample * volumes[plane] >> 12);
    }
    sasEnvelope(voice);
    if(ended || !voice.playing) continue;
    u32 fraction = (voice.position & 0xfff) + voice.pitch;
    voice.position = (voice.position & ~0xfffull) | (fraction & 0xfff);
    for(u32 step = 0; step < fraction >> 12 && !ended; step++) {
      if(voice.source == Sas::Source::Vag) {
        ended = !sasNext(voice, bytes);
      } else if(voice.source == Sas::Source::Pcm) {
        u64 sample = (voice.position >> 12) + 1;
        if(sample >= voice.size && voice.loop < 0) ended = true;
        else voice.position = (sample < voice.size ? sample : u64(voice.loop)) << 12 | (voice.position & 0xfff);
      } else {
        voice.position += 0x1000;
      }
    }
  }
  if(ended || !voice.playing) {
    voice.position = begin;
    voice.playing = voice.on = false;
  }
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

//A grain made (see the top): every playing voice that isn't paused adds its samples to the sums (a voice keyed on
//waiting its first 32 first), and the end flags are refreshed; then the grain is written to the buffer, a grain of
//stereo pairs (stereo) or four planes of a grain each (multichannel). Mixing (__sceSasCoreWithMix: the buffer's left
//and right volumes in arguments 2 and 3) adds the voices to what the buffer holds, scaled by those, in stereo alone:
//multichannel is refused (NOT_SUPPORTED, as audio/sascore/outputmode recorded), before anything moves. First of all,
//where a thread can't wait it's refused as a function that waits is (mayWait()), the buffer and the voices left as
//they were: pspautotests' intr/delays recorded __sceSasCore, whose grain waits for the Media Engine on a PSP, refused
//in an interrupt handler (ILLEGAL_CONTEXT) and with interrupts or dispatching held off (CAN_NOT_WAIT).
//__sceSasCoreWithMix waits alike, and is taken the same (not recorded).
auto Kernel::sasMix(bool mix) -> void {
  if(!mayWait()) return;
  u32 buffer = arg(1);
  if(!sas.initialized) return result(SasErrorNotInitialized);
  u32 grain = sas.grain, bytes = grain * (sas.outputMode ? 8 : 4);
  if(!buffer || !memory.reaches(buffer, bytes)) return result(SasErrorAddress);
  if(mix && sas.outputMode) return result(ErrorNotSupported);
  sasSums.assign(grain * 4, 0);
  sasSamples.resize(grain * 4);
  for(u32 number = 0; number < 32; number++) {
    auto& voice = sas.voices[number];
    if(!voice.playing || (sas.paused >> number & 1)) continue;
    u32 waits = std::min(voice.delay, grain);
    voice.delay -= waits;
    sasGrain(voice, waits, sasSums.data());
  }
  sas.endFlags = 0;
  for(u32 number = 0; number < 32; number++) if(!sas.voices[number].playing) sas.endFlags |= 1u << number;

  auto clamp = [](s64 sum) { return s16(std::clamp<s64>(sum, -32768, 32767)); };
  const s32 *dryLeft = &sasSums[0], *dryRight = &sasSums[grain], *wetLeft = &sasSums[grain * 2];
  const s32* wetRight = &sasSums[grain * 3];
  bool dry = sas.effectDry, wet = sas.effectWet;
  if(sas.outputMode) {  //four planes: dry left, dry right, wet left, wet right
    for(u32 n = 0; n < grain; n++) {
      sasSamples[n] = clamp(dry ? dryLeft[n] : 0);
      sasSamples[grain + n] = clamp(dry ? dryRight[n] : 0);
      sasSamples[grain * 2 + n] = clamp(wet ? wetLeft[n] : 0);
      sasSamples[grain * 3 + n] = clamp(wet ? wetRight[n] : 0);
    }
  } else {  //left and right pairs: the dry sound, the wet sound through the effect (passed through as it is)
    if(mix) memory.copyOut(sasSamples.data(), buffer, bytes);
    s64 mixLeft = s32(arg(2)), mixRight = s32(arg(3));
    s64 effectLeft = sas.effectLeft, effectRight = sas.effectRight;
    bool effect = wet && sas.effectType >= 0;
    for(u32 n = 0; n < grain; n++) {
      s64 left = dry ? dryLeft[n] : 0, right = dry ? dryRight[n] : 0;
      if(effect) left += wetLeft[n] * effectLeft >> 12, right += wetRight[n] * effectRight >> 12;
      if(mix) left += sasSamples[n * 2] * mixLeft >> 12, right += sasSamples[n * 2 + 1] * mixRight >> 12;
      sasSamples[n * 2] = clamp(left);
      sasSamples[n * 2 + 1] = clamp(right);
    }
  }
  memory.copyIn(buffer, sasSamples.data(), bytes);
  result(0);
}

//(core, buffer)
auto Kernel::__sceSasCore() -> void {
  sasMix(false);
}

//(core, buffer, left volume, right volume): the voices added to what's in the buffer, that scaled by the volumes
//first (0x1000 as it is)
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
  if(voice->source != Sas::Source::Vag) {
    voice->position = 0, voice->loopBlock = 0;
    voice->decoded[0] = voice->decoded[1] = 0, voice->started = false;
  }
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
  voice->decoded[0] = voice->decoded[1] = 0, voice->started = false;
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
  voice->decoded[0] = voice->decoded[1] = 0, voice->started = false;
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
