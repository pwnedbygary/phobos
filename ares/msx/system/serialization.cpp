//[Phobos] v135: the Expansion Slot is left empty, and a state holds the board of each slot with a cartridge in,
//so states from when the game was in both slots don't load. v134 left out the tape's samples and kept port C.
static const string SerializerVersion = "v135";

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

  if(synchronize) power();
  serialize(s, synchronize);
  return true;
}

auto System::serialize(serializer& s, bool synchronize) -> void {
  scheduler.setSynchronize(synchronize);
  keyboard.serialize(s);
  cpu.serialize(s);
  vdp.serialize(s);
  psg.serialize(s);
  cartridge.serialize(s);
  expansion.serialize(s);
  controllerPort1.serialize(s);
  controllerPort2.serialize(s);
  tapeDeck.tray.tape.serialize(s);
  rtc.serialize(s);
}
