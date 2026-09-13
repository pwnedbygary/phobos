// NOTE: A real Dual Shock starts up in Digital Mode
// However, we start up in Analog Mode due to there not being
// enough buttons to properly map the "Analog" button on the controller

#if defined(__ANDROID__)
#include <android/log.h>
#endif

DualShock::DualShock(Node::Port parent) {
  node = parent->append<Node::Peripheral>("DualShock");

  axis = node->append<Node::Input::Axis>("Axis");

  lx       = node->append<Node::Input::Axis  >("L-Stick X");
  ly       = node->append<Node::Input::Axis  >("L-Stick Y");
  rx       = node->append<Node::Input::Axis  >("R-Stick X");
  ry       = node->append<Node::Input::Axis  >("R-Stick Y");
  up       = node->append<Node::Input::Button>("Up");
  down     = node->append<Node::Input::Button>("Down");
  left     = node->append<Node::Input::Button>("Left");
  right    = node->append<Node::Input::Button>("Right");
  cross    = node->append<Node::Input::Button>("Cross");
  circle   = node->append<Node::Input::Button>("Circle");
  square   = node->append<Node::Input::Button>("Square");
  triangle = node->append<Node::Input::Button>("Triangle");
  l1       = node->append<Node::Input::Button>("L1");
  l2       = node->append<Node::Input::Button>("L2");
  l3       = node->append<Node::Input::Button>("L3");
  r1       = node->append<Node::Input::Button>("R1");
  r2       = node->append<Node::Input::Button>("R2");
  r3       = node->append<Node::Input::Button>("R3");
  select   = node->append<Node::Input::Button>("Select");
  start    = node->append<Node::Input::Button>("Start");
  mode     = node->append<Node::Input::Button>("Mode");
  rumble   = node->append<Node::Input::Rumble>("Rumble");

  analogMode = 1;
  newRumbleMode = 0;
  configMode = 0;
}

auto DualShock::reset() -> void {
  state = State::Idle;
  _active = false;
  outputData.clear();
  analogMode = 1;
  configMode = 0;
}

auto DualShock::acknowledge() -> bool {
  return state != State::Idle;
}

auto DualShock::toggleAnalogMode() -> bool {
  setAnalogMode(!analogMode);
  return true;
}

// Serve the next queued response byte without requiring a host byte first.
// Ape Escape sends only `01 42` per poll and reads the full 8-byte response
// from SIO RX via Peripheral::receive(); popping here hands the queued bytes
// straight to the game (the bus() select-path never drains them).
auto DualShock::popResponse() -> s32 {
  if(outputData.empty()) return -1;
  u8 out = outputData.front();
  outputData.erase(outputData.begin());
  commandStep++;
  if(outputData.empty()) {
    commandStep = 0;
    state = State::Idle;
  }
  return out;
}

auto DualShock::active() -> bool {
  return _active || acknowledge();
}

auto DualShock::bus(u8 data) -> u8 {
  n8 input  = data;
  n8 output = 0xff;

  //old rumble mode
  if(!newRumbleMode && command == 0x42) {
    switch(commandStep) {
      case 1: inputData.clear(); inputData.push_back(input); break;
      case 2: rumble->setEnable(inputData[0].bit(6, 7) == 1 && input.bit(0) == 1); break;
    }
    platform->input(rumble);
  }

  //new rumble mode
  if(newRumbleMode && command == 0x42 && commandStep > 0) {
    auto index = commandStep - 1;
    if(rumbleConfig[index] == 0x00) rumble->setWeak(input.bit(0) ? 0xffff : 0); // small motor
    if(rumbleConfig[index] == 0x01) rumble->setStrong(input * 65535 / 255);     // large motor
    platform->input(rumble);
  }

  //config Mode Enable/Disable
  if(command == 0x43 && commandStep == 0) {
    configMode = input;
    newRumbleMode = 1;
  }

  //set led state / analog mode
  if(command == 0x44 && commandStep == 0) {
    analogMode = input;
    for(auto n : range(6)) rumbleConfig[n] = 0xff;
  }

  //variable response A
  if(command == 0x46 && commandStep == 0) {
    if(input == 0x00)      { outputData.insert(outputData.end(), {0x01,0x02,0x00,0x00}); }
    else if(input == 0x01) { outputData.insert(outputData.end(), {0x01,0x01,0x01,0x14}); }
    else                   { outputData.insert(outputData.end(), {0x00,0x00,0x00,0x00}); }
  }

  //variable response B
  if(command == 0x4c && commandStep == 0) {
    u8 value = 0x00;
    if(input == 0x00) value = 0x04;
    if(input == 0x01) value = 0x07;
    outputData.insert(outputData.end(), {value,0x00,0x00});
  }

  if(command == 0x4d && commandStep >= 1) {
    rumbleConfig[commandStep - 1] = input;
  }

  //if there is data in the output queue, return that
  // Ape Escape sends `01 42` and does NOT clock out the response bytes (it
  // reads them from SIO RX instead). The next `01` select must NOT pop a stale
  // response byte (that shifts every poll by one); it starts a fresh transfer
  // below (Idle clears the leftover queue).
  if(outputData.size() > 0 && input != 0x01) {
    output = outputData.front();
    outputData.erase(outputData.begin());
    commandStep++;
    if(outputData.size() == 0) {
      commandStep = 0;
      state = State::Idle;
    }
    return output;
  }

  switch(state) {

  case State::Idle: {
    command = 0;
    // Fresh select: discard any leftover response from a poll the game didn't
    // fully clock out, so the byte stream stays aligned.
    outputData.clear();
    commandStep = 0;
    if(input != 0x01) {
      _active = false;
      break;
    }

    output = 0xff;
    state = State::IDLower;
    _active = true;
    break;
  }

  case State::IDLower: {
    command = input;

    if(configMode) output = 0xf3;
    else output = analogMode ? 0x73 : 0x41;
    outputData.push_back(0x5a);

    //Global commands: these work during any operation mode
    switch(input) {
      case 0x42: {
        auto v = readPad();
        outputData.insert(outputData.end(), v.begin(), v.end());
      } break;
      case 0x43: {
        if(configMode) { outputData.insert(outputData.end(), {0x00,0x00,0x00,0x00,0x00,0x00}); }
        else {
          auto v = readPad();
          outputData.insert(outputData.end(), v.begin(), v.end());
        } break;
      default:
        if(configMode) {
          switch(input) {
            case 0x44: { outputData.insert(outputData.end(), {0x00,0x00,0x00,0x00,0x00,0x00}); } break;
            case 0x45: { outputData.insert(outputData.end(), {0x01,0x02,(u8)analogMode,0x02,0x01,0x00}); } break;
            case 0x46: { outputData.insert(outputData.end(), {0x00,0x00}); } break; // Partial response, will be completed on step 1
            case 0x47: { outputData.insert(outputData.end(), {0x00,0x00,0x02,0x00,0x01,0x00}); } break;
            case 0x4c: { outputData.insert(outputData.end(), {0x00,0x00,0x00}); } break; // Partial response, will be completed on step 1
            case 0x4d: for(auto n : range(6)) outputData.push_back(rumbleConfig[n]); break;
            default:
              outputData.clear();
              output = invalid(input);
              break;
          }
          break;
        }

        outputData.clear();
        output = invalid(input);
        break;
      }
    }
    break;
  }

  }

  return output;
}

auto DualShock::readPad() -> std::vector<u8> {
  std::vector<u8> result;
  n8 output;

  platform->input(select);
  platform->input(l3);
  platform->input(r3);
  platform->input(start);
  platform->input(up);
  platform->input(right);
  platform->input(down);
  platform->input(left);

  output.bit(0) = !select->value();
  output.bit(1) = analogMode || configMode ? !l3->value() : 1;
  output.bit(2) = analogMode || configMode ? !r3->value() : 1;
  output.bit(3) = !start->value();
  output.bit(4) = !(up->value() & !down->value());
  output.bit(5) = !(right->value() & !left->value());
  output.bit(6) = !(down->value() & !up->value());
  output.bit(7) = !(left->value() & !right->value());
  result.push_back(output);

  platform->input(l2);
  platform->input(r2);
  platform->input(l1);
  platform->input(r1);
  platform->input(triangle);
  platform->input(circle);
  platform->input(cross);
  platform->input(square);

  output.bit(0) = !l2->value();
  output.bit(1) = !r2->value();
  output.bit(2) = !l1->value();
  output.bit(3) = !r1->value();
  output.bit(4) = !triangle->value();
  output.bit(5) = !circle->value();
  output.bit(6) = !cross->value();
  output.bit(7) = !square->value();
  result.push_back(output);

  if(!analogMode && !configMode) return result;

  // LINEAR response mapping (Phobos): replace ares' aggressive response curve
  // (which saturates ~30% of stick travel and makes small deflections nearly
  // dead — the "character twitches instead of walking" symptom) with a clean
  // linear map: full stick deflection -> full 0..255 byte range. This matches
  // the smooth, immediate analog feel of the N64 core. A tiny deadzone (~3%)
  // absorbs stick noise at rest only.
  const double half = 127.5;
  const double deadzone = 4.0;  // raw units out of ±32767 (~0.012%)

  auto linearize = [&](s64 raw) -> u8 {
    if(raw > -deadzone && raw < deadzone) return (u8)128;  // centered
    double v = (double)raw / 32767.0;                      // -1..1
    if(v < -1.0) v = -1.0;
    if(v > 1.0) v = 1.0;
    return (u8)(v * half + half);                          // 0..255
  };

  platform->input(rx);
  platform->input(ry);
  u8 bRX = linearize(rx->value());
  u8 bRY = linearize(ry->value());
  result.push_back(bRX);
  result.push_back(bRY);

  platform->input(lx);
  platform->input(ly);
  u8 bLX = linearize(lx->value());
  u8 bLY = linearize(ly->value());

  // TEMP DIAG: report the full packet the game receives (buttons + sticks).
  #if defined(__ANDROID__)
  static u64 readTraceCount = 0;
  if(readTraceCount++ % 30 == 0) {
    __android_log_print(ANDROID_LOG_INFO, "PhobosDualShock",
      "analogMode=%d configMode=%d B1=%02x B2=%02x RX=%u RY=%u LX=%u LY=%u",
      (int)analogMode, (int)configMode,
      (unsigned)result[0], (unsigned)result[1],
      (unsigned)bRX, (unsigned)bRY, (unsigned)bLX, (unsigned)bLY);
  }
  #endif

  result.push_back(bLX);
  result.push_back(bLY);

  return result;
}

auto DualShock::invalid(u8 data) -> u8 {
  debug(unusual, "[DualShock] Invalid command byte ", hex(data));
  _active = false;
  state = State::Idle;
  return 0xff;
}
