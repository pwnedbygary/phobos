TapeDeck tapeDeck{"Tape Deck"};

TapeDeck::TapeDeck(string name) : name(name) {
}

auto TapeDeck::load(Node::Object parent) -> void {
  node = parent->append<Node::Peripheral>(name);

  tray.load(node);
}

auto TapeDeck::power() -> void {
}

auto TapeDeck::motor(bool on) -> void {
  auto& tape = tray.tape.node;
  if(!tape || tape->length() == 0) return;
  if(on && !tape->playing() && tape->position() < tape->length()) tape->play();
  if(!on && tape->playing()) tape->stop();
}

auto TapeDeck::rewind() -> void {
  auto& tape = tray.tape.node;
  if(!tape || tape->length() == 0) return;
  tape->stop();
  tape->setPosition(0);
  motor(cpu.cassetteMotor());
}

auto TapeDeck::unload() -> void {
  tray.unload();
  node = {};
}
