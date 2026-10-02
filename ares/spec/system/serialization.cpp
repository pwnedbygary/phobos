static const string SerializerVersion = "v133";

auto System::serialize(bool synchronize) -> serializer {
  if(synchronize) scheduler.enter(Scheduler::Mode::Synchronize);
  serializer s;

  u32  signature = SerializerSignature;
  char version[16] = {};
  char description[512] = {};
  u32  model = (u32)information.model;
  memory::copy(&version, (const char*)SerializerVersion, SerializerVersion.size());

  s(signature);
  s(synchronize);
  s(version);
  s(description);
  s(model);
  serializeAll(s, synchronize);
  return s;
}

auto System::unserialize(serializer& s) -> bool {
  u32  signature = 0;
  bool synchronize = true;
  char version[16] = {};
  char description[512] = {};
  u32  model = 0;

  s(signature);
  s(synchronize);
  s(version);
  s(description);
  s(model);

  if(signature != SerializerSignature) return false;
  if(string{version} != SerializerVersion) return false;
  // [Phobos] A 48K and a 128K state differ in layout (the RAM's size, the AY chip), so neither loads into the other.
  if(model != (u32)information.model) return false;

  if(synchronize) power(/* reset =*/ false);
  serializeAll(s, synchronize);
  return true;
}

//internal

auto System::serialize(serializer& s) -> void {
  s(romBank);
  s(screenBank);
  s(ramBank);
  s(pagingDisabled);
}

auto System::serializeAll(serializer& s, bool synchronize) -> void {
  scheduler.setSynchronize(synchronize);
  system.serialize(s);
  cpu.serialize(s);
  ula.serialize(s);
  if(model() == Model::Spectrum128) psg.serialize(s);
  tapeDeck.serialize(s);
}
