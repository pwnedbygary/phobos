struct TapeDeck {
  Node::Peripheral node;
  TapeDeckTray tray;

  TapeDeck(string name);

  auto playing() -> bool { return tray.tape.node && tray.tape.node->playing(); }
  auto read() -> u1 { return tray.tape.read(); }
  auto write(n1 data) -> void { tray.tape.write(data); }

  //[Phobos] The cassette motor relay (bit 4 of the PPI's port C): a loaded tape plays while the relay is
  //on and stops where it is when it goes off, as a deck plugged into the MSX's remote socket does, so
  //BASIC's CLOAD, BLOAD and RUN"CAS:" and games' own loaders start and stop it with no detection.
  auto motor(bool on) -> void;
  //[Phobos] Winds the tape back to its start; it plays from there if the motor is on.
  auto rewind() -> void;
  //[Phobos] With recording armed, the relay records the cassette output (bit 5 of port C) onto the end
  //of a tape that takes recording, instead of playing it, as with a deck's record button held down.
  n1 recordArmed;

  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto power() -> void;

  const string name;
};

extern TapeDeck tapeDeck;
