//sceP3da, the positional 3D audio library (Sony's, in the SAS core's module since 2.80): a game's sound channels,
//each a run of mono samples, mixed into stereo samples. The NIDs are their names' hashes (sceP3daBridgeInit,
//sceP3daBridgeCore, sceP3daBridgeExit); no source here describes the arguments, so they're what Sol Trigger's sound
//thread passes and does with them, followed through its code:
//  - sceP3daBridgeInit(channels, samples): Sol Trigger gives 4 and 0x800, and goes on only when 0 comes back.
//  - sceP3daBridgeCore(the positions' work area, channels, samples, the channels' buffers, stereo buffer): the
//    channels' buffers an array of that many addresses, each of samples 16-bit mono samples (Sol Trigger's 0x800
//    each, 0x1040 bytes apart); the stereo buffer samples pairs, left then right. The work area holds what places each
//    channel (two addresses, in Sol Trigger's), in a layout nothing describes.
//  - sceP3daBridgeExit().
//Chosen: the channels are mixed with no place given them, each heard alike in both ears, their sum held to 16 bits;
//a channel whose samples aren't in memory is left out. Every call returns 0. (The PSP's own placing, by the work area,
//isn't known, so a sound Sol Trigger places to one side is heard in the middle.)

//(channels, samples)
auto Kernel::sceP3daBridgeInit() -> void {
  result(0);
}

//(work area, channels, samples, the channels' buffers, stereo buffer)
auto Kernel::sceP3daBridgeCore() -> void {
  u32 channels = arg(1), samples = arg(2), buffers = arg(3), output = arg(4);
  if(!samples || samples > 0x10000 || channels > 64 || !memory.reaches(output, samples * 4)) return result(0);
  std::vector<s32> sum(samples, 0);
  std::vector<s16> mono(samples);
  for(u32 channel = 0; channel < channels; channel++) {
    if(!memory.reaches(buffers + channel * 4, 4)) break;
    u32 address = memory.read(4, buffers + channel * 4);
    if(!memory.reaches(address, samples * 2)) continue;
    memory.copyOut(mono.data(), address, samples * 2);
    for(u32 n = 0; n < samples; n++) sum[n] += mono[n];
  }
  std::vector<s16> stereo(samples * 2);
  for(u32 n = 0; n < samples; n++) stereo[n * 2] = stereo[n * 2 + 1] = std::clamp(sum[n], -32768, 32767);
  memory.copyIn(output, stereo.data(), samples * 4);
  result(0);
}

//()
auto Kernel::sceP3daBridgeExit() -> void {
  result(0);
}
