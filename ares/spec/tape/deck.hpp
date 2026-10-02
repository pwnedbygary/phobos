struct TapeDeck {
  Node::Peripheral node;
  TapeDeckTray tray;

  TapeDeck(string name);

  auto playing() -> bool { return tray.tape.node && tray.tape.node->playing(); }
  auto read() -> u1 { reads++; return tray.tape.read(); }

  // [Phobos] Reads of the playing tape's signal since Phobos last took the count (its tape loading
  // speed boosts the game while a loader reads the tape).
  u32 reads = 0;

  // [Phobos] With autoControl on, plays the tape while a loader reads it and stops it once the game
  // moves on, the way Fuse detects loaders: a loader times the tape's edges in a loop that reads port
  // $FE at most 500 T-states apart while it counts in B, so B moves by one between reads. ULA::in()
  // calls this for every read of the port. A stop by the user (heldByUser) lasts until the game moves on.
  auto detectLoader() -> void;
  std::atomic<bool> autoControl{false};
  std::atomic<bool> heldByUser{false};

  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto power() -> void;
  auto serialize(serializer&) -> void;

  const string name;

private:
  u64 lastReadCycle = 0;
  u8 lastReadB = 0;
  u32 successiveReads = 0;
};

extern TapeDeck tapeDeck;
