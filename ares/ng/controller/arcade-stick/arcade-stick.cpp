ArcadeStick::ArcadeStick(Node::Port parent) {
  node = parent->append<Node::Peripheral>("Arcade Stick");

  up     = node->append<Node::Input::Button>("Up");
  down   = node->append<Node::Input::Button>("Down");
  left   = node->append<Node::Input::Button>("Left");
  right  = node->append<Node::Input::Button>("Right");
  a      = node->append<Node::Input::Button>("A");
  b      = node->append<Node::Input::Button>("B");
  c      = node->append<Node::Input::Button>("C");
  d      = node->append<Node::Input::Button>("D");
  select = node->append<Node::Input::Button>("Select");
  start  = node->append<Node::Input::Button>("Start");
}

auto ArcadeStick::readButtons() -> n8 {
  platform->input(up);
  platform->input(down);
  platform->input(left);
  platform->input(right);
  platform->input(a);
  platform->input(b);
  platform->input(c);
  platform->input(d);
  // Drive MVS coin assertion: holding START auto-inserts a credit (handheld
  // convenience) and SELECT acts as the coin button. Read start/select here
  // (readButtons runs before REG_STATUS_A in CPU::readIO) so the coin line is
  // already asserted when the BIOS polls REG_STATUS_A in the same iteration.
  platform->input(start);
  platform->input(select);
  //SELECT acts as the coin button: emit a oneshot pulse on its rising edge so
  //the BIOS coin counter sees a clean high->low->high transition even if the
  //button is held (a held level is never sampled as an edge and counts no coin).
  bool sel = select->value();
  if(sel && !prevSelect) system.io.coinPulseTimer = 600'000;  //~3 frames at 12MHz
  prevSelect = sel;
  system.io.coin = start->value() || system.io.coinPulse;

  if(!(up->value() && down->value())) {
    yHold = 0, upLatch = up->value(), downLatch = down->value();
  } else if(!yHold) {
    yHold = 1, swap(upLatch, downLatch);
  }

  if(!(left->value() && right->value())) {
    xHold = 0, leftLatch = left->value(), rightLatch = right->value();
  } else if(!xHold) {
    xHold = 1, swap(leftLatch, rightLatch);
  }

  n8 data;
  data.bit(0) = upLatch;
  data.bit(1) = downLatch;
  data.bit(2) = leftLatch;
  data.bit(3) = rightLatch;
  data.bit(4) = a->value();
  data.bit(5) = b->value();
  data.bit(6) = c->value();
  data.bit(7) = d->value();
  return ~data;
}

auto ArcadeStick::readControls() -> n2 {
  platform->input(select);
  platform->input(start);
  //keep coin line / pulse assertion in sync regardless of which register the game polls
  bool sel = select->value();
  if(sel && !prevSelect) system.io.coinPulseTimer = 600'000;
  prevSelect = sel;
  system.io.coin = start->value() || system.io.coinPulse;

  n2 data;
  data.bit(0) = start->value();
  data.bit(1) = select->value();
  return ~data;
}
