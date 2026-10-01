struct TapeDeck {
  Node::Peripheral node;
  TapeDeckTray tray;

  TapeDeck(string name);

  auto playing() -> bool { return tray.tape.node && tray.tape.node->playing(); }
  auto read() -> u1 { reads++; return tray.tape.read(); }

  // [Phobos] Reads of the playing tape's signal since Phobos last took the count (its tape loading
  // speed boosts the game while a loader reads the tape).
  u32 reads = 0;

  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto power() -> void;

  const string name;
};

extern TapeDeck tapeDeck;
