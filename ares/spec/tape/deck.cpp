TapeDeck tapeDeck{"Tape Deck"};

TapeDeck::TapeDeck(string name) : name(name) {
}

auto TapeDeck::load(Node::Object parent) -> void {
  node = parent->append<Node::Peripheral>(name);

  tray.load(node);
}

auto TapeDeck::power() -> void {
  // [Phobos] A new program starts with no loader seen and no stop held over from the last one.
  lastReadCycle = cpu.cycles;
  lastReadB = 0;
  successiveReads = 0;
  heldByUser = false;
}

auto TapeDeck::unload() -> void {
  tray.unload();
  node = {};
}

auto TapeDeck::detectLoader() -> void {
  u64 elapsed = cpu.cycles - lastReadCycle;
  u8 b = cpu.bc.byte.hi;
  u8 step = b - lastReadB;
  lastReadCycle = cpu.cycles;
  lastReadB = b;

  auto& tape = tray.tape.node;
  if(!autoControl || !tape || tape->length() == 0) {
    successiveReads = 0;
    return;
  }
  bool loaderRead = elapsed <= 500 && (step == 1 || step == 0xff);
  bool otherRead = elapsed > 1000 || (step != 0 && step != 1 && step != 0xff);

  if(tape->playing() || heldByUser) {
    if(!otherRead) {
      successiveReads = 0;
    } else if(++successiveReads >= 2) {
      successiveReads = 0;
      tape->stop();
      heldByUser = false;
    }
    return;
  }

  if(loaderRead && tape->position() < tape->length()) {
    if(++successiveReads >= 10) {
      successiveReads = 0;
      tape->play();
    }
  } else {
    successiveReads = 0;
  }
}
