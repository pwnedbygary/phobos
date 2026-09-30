static const string SerializerVersion = "v134";
//Neo Geo CD states carry a format number of their own after the header, so a CD state from before a
//CD-only change is refused instead of read misaligned, while AES and MVS states stay valid.
//1: the Z80's 64KiB of RAM and its reset line, and the $FF0004 interrupt mask.
static const u32 CDSerializerFormat = 1;

auto System::serialize(bool synchronize) -> serializer {
  if(synchronize) scheduler.enter(Scheduler::Mode::Synchronize);
  serializer s;

  u32  signature = SerializerSignature;
  char version[16] = {};
  char description[512] = {};
  memory::copy(&version, (const char*)SerializerVersion, SerializerVersion.size());

  s(signature);
  s(synchronize);
  s(version);
  s(description);
  if(NeoGeo::Model::NeoGeoCD()) {
    u32 cdFormat = CDSerializerFormat;
    s(cdFormat);
  }

  serialize(s, synchronize);
  return s;
}

auto System::unserialize(serializer& s) -> bool {
  u32  signature = 0;
  bool synchronize = true;
  char version[16] = {};
  char description[512] = {};

  s(signature);
  s(synchronize);
  s(version);
  s(description);

  if(signature != SerializerSignature) return false;
  if(string{version} != SerializerVersion) return false;
  if(NeoGeo::Model::NeoGeoCD()) {
    u32 cdFormat = 0;
    s(cdFormat);
    if(cdFormat != CDSerializerFormat) return false;
  }

  if(synchronize) power(/* reset = */ false);
  serialize(s, synchronize);
  return true;
}

auto System::serialize(serializer& s, bool synchronize) -> void {
  scheduler.setSynchronize(synchronize);
  s(cartridge);
  s(controllerPort1);
  s(controllerPort2);
  s(cardSlot);
  s(cpu);
  s(apu);
  s(lspc);
  s(opnb);
  s(wram);
  s(sram);
  s(io.sramLock);
  s(io.slotSelect);
  s(io.ledMarquee);
  s(io.ledLatch1);
  s(io.ledLatch2);
  s(io.ledData);
  s(io.rtcCounter);
  s(io.rtcTimePulse);
  if(NeoGeo::Model::NeoGeoCD()) s(io.irqMask2);
}
