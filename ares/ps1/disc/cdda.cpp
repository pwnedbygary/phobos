auto Disc::CDDA::load(Node::Object parent) -> void {
//stream = parent->append<Node::Audio::Stream>("CD-DA");
//stream->setChannels(2);
//stream->setFrequency(44100);
}

auto Disc::CDDA::unload(Node::Object parent) -> void {
//parent->remove(stream);
//stream.reset();
}

auto Disc::CDDA::clockSector() -> void {
}

auto Disc::CDDA::clockSample() -> void {
  s16 left  = 0;
  s16 right = 0;

  auto track = drive->session->track(drive->sector.track);
  auto isData = track ? track->isData() : true;

  bool isCurrentlyPlaying = self.ssr.reading && self.ssr.playingCDDA && !isData;

  // Detect state transitions and set fade target
  if(isCurrentlyPlaying != previousPlayingState) {
    previousPlayingState = isCurrentlyPlaying;
    // Fade duration: 150 samples @ 44.1kHz = ~3.4ms (sweet spot between click elimination and swooping)
    const f32 fadeSamples = 150.0f;
    cddaGainTarget = isCurrentlyPlaying ? 1.0f : 0.0f;
    cddaFadeRate = 1.0f / fadeSamples;
  }

  // Apply fade envelope: move current gain toward target at constant rate
  if(cddaGain != cddaGainTarget) {
    if(cddaGain < cddaGainTarget) {
      cddaGain = std::min(cddaGain + cddaFadeRate, cddaGainTarget);
    } else {
      cddaGain = std::max(cddaGain - cddaFadeRate, cddaGainTarget);
    }
  }

  if(isCurrentlyPlaying) {
    left  |= drive->sector.data[drive->sector.offset++] << 0;
    left  |= drive->sector.data[drive->sector.offset++] << 8;
    right |= drive->sector.data[drive->sector.offset++] << 0;
    right |= drive->sector.data[drive->sector.offset++] << 8;
  }

  if(self.audio.mute) {
    sample.left  = 0;
    sample.right = 0;
  } else {
    //each channel is saturated; but the combination of each channel is not and may overflow
    s32 leftScaled  = sclamp<16>(left * self.audio.volume[0] >> 7) + sclamp<16>(right * self.audio.volume[2] >> 7);
    s32 rightScaled = sclamp<16>(left * self.audio.volume[1] >> 7) + sclamp<16>(right * self.audio.volume[3] >> 7);
    
    // Apply fade envelope to eliminate discontinuities
    sample.left  = sclamp<16>(leftScaled * cddaGain);
    sample.right = sclamp<16>(rightScaled * cddaGain);
  }

//stream->sample(sample.left / 32768.0, sample.right / 32768.0);
}
