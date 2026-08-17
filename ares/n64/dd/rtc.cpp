auto DD::RTC::load() -> void {
  ram.allocate(0x10);
  if(auto fp = system.pak->read("time.rtc")) {
    ram.load(fp);
  }

  //byte 0 to 7 = raw rtc time (last updated, only 6 bytes are used)
  n64 check = 0;
  for(auto n : range(8)) check.byte(n) = ram.read<Byte>(n);
  __android_log_print(ANDROID_LOG_WARN, "PhobosDD",
    "RTC load: %02x%02x %02x%02x %02x%02x %02x%02x | ts=%08llx | check=%016llx new=%d valid=%d",
    ram.read<Byte>(0), ram.read<Byte>(1), ram.read<Byte>(2), ram.read<Byte>(3),
    ram.read<Byte>(4), ram.read<Byte>(5), ram.read<Byte>(6), ram.read<Byte>(7),
    (unsigned long long)ram.read<Dual>(8),
    (unsigned long long)check, !~check ? 1 : 0, valid() ? 1 : 0);
  if(!~check || check == 0) {  //new save file (all-0xFF erased EEPROM OR all-zero placeholder)
    // [Phobos] Seed a fresh 64DD RTC with the current host time in BCD.
    // On real hardware the RTC comes pre-set from the factory; leaving it
    // all-0xFF (or the all-zero node Phobos creates for a missing time.rtc)
    // makes the game read invalid time data -> "Error 48 —
    // Date/Time not set" on every boot (F-Zero X Expansion Kit).
    seedCurrentTime();
    return;
  }

  //check for invalid time info, if invalid, set time info to something invalid and ignore the rest
  if (!valid()) {
    for(auto n : range(8)) ram.write<Byte>(n, 0xff);
    return;
  }

  //byte 8 to 15 = timestamp of when the last save was made
  n64 timestamp = 0;
  for(auto n : range(8)) timestamp.byte(n) = ram.read<Byte>(8 + n);
  if(!~timestamp) return;  //new save file

  //update based on the amount of time that has passed since the last save
  timestamp = time(0) - timestamp;
  while(timestamp--) tickSecond();
}

// [Phobos] Write the current wall-clock time into the RTC as BCD
// (byte 0=year, 1=month, 2=day, 3=hour, 4=minute, 5=second), plus the save
// timestamp at ram[8..15] so the elapsed-time update in load() works on later
// boots. Matches the byte layout used by tickSecond()/controller.cpp.
auto DD::RTC::seedCurrentTime() -> void {
  time_t t = time(0);
  struct tm* lt = localtime(&t);
  if(!lt) return;
  ram.write<Byte>(0, BCD::encode(u8(lt->tm_year % 100)));  //year (0-99)
  ram.write<Byte>(1, BCD::encode(u8(lt->tm_mon + 1)));     //month (1-12)
  ram.write<Byte>(2, BCD::encode(u8(lt->tm_mday)));        //day (1-31)
  ram.write<Byte>(3, BCD::encode(u8(lt->tm_hour)));        //hour (0-23)
  ram.write<Byte>(4, BCD::encode(u8(lt->tm_min)));         //minute (0-59)
  ram.write<Byte>(5, BCD::encode(u8(lt->tm_sec)));         //second (0-59)
  ram.write<Byte>(6, 0);
  ram.write<Byte>(7, 0);
  n64 timestamp = (n64)t;
  for(auto n : range(8)) ram.write<Byte>(8 + n, timestamp.byte(n));
  __android_log_print(ANDROID_LOG_WARN, "PhobosDD",
    "RTC seed: %02x%02x %02x%02x %02x%02x %02x%02x | ts=%08llx",
    ram.read<Byte>(0), ram.read<Byte>(1), ram.read<Byte>(2), ram.read<Byte>(3),
    ram.read<Byte>(4), ram.read<Byte>(5), ram.read<Byte>(6), ram.read<Byte>(7),
    (unsigned long long)timestamp);
}

auto DD::RTC::reset() -> void {
  ram.reset();
}

auto DD::RTC::save() -> void {
  n64 timestamp = time(0);
  for(auto n : range(8)) ram.write<Byte>(8 + n, timestamp.byte(n));

  if(auto fp = system.pak->write("time.rtc")) {
    ram.save(fp);
  }
  __android_log_print(ANDROID_LOG_WARN, "PhobosDD",
    "RTC save: %02x%02x %02x%02x %02x%02x %02x%02x | ts=%08llx",
    ram.read<Byte>(0), ram.read<Byte>(1), ram.read<Byte>(2), ram.read<Byte>(3),
    ram.read<Byte>(4), ram.read<Byte>(5), ram.read<Byte>(6), ram.read<Byte>(7),
    (unsigned long long)timestamp);
}

auto DD::RTC::serialize(serializer& s) -> void {
  s(ram);
}

auto DD::RTC::tick(u32 offset) -> void {
  u8 n = ram.read<Byte>(offset);
  if((++n & 0xf) > 9) n = (n & 0xf0) + 0x10;
  if((n & 0xf0) > 0x90) n = 0;
  ram.write<Byte>(offset, n);
}

auto DD::RTC::tickClock() -> void {
  tickSecond();
  queue.remove(Queue::DD_Clock_Tick);
  cpu.queueInsert(Queue::DD_Clock_Tick, 187'500'000);
}

auto DD::RTC::tickSecond() -> void {
  if (!valid()) return;

  //second
  tick(5);
  if(ram.read<Byte>(5) < 0x60) return;
  ram.write<Byte>(5, 0);

  //minute
  tick(4);
  if(ram.read<Byte>(4) < 0x60) return;
  ram.write<Byte>(4, 0);

  //hour
  tick(3);
  if(ram.read<Byte>(3) < 0x24) return;
  ram.write<Byte>(3, 0);

  //day
  tick(2);
  if(ram.read<Byte>(2) <= BCD::encode(chrono::daysInMonth(BCD::decode(ram.read<Byte>(1)), BCD::decode(ram.read<Byte>(0))))) return;
  ram.write<Byte>(2, 1);

  //month
  tick(1);
  if(ram.read<Byte>(1) <= 0x12) return;
  ram.write<Byte>(1, 1);

  //year
  tick(0);
}

auto DD::RTC::valid() -> bool {
  //check validity of ram rtc data (if it's BCD valid or not)
  for(auto n : range(6)) {
    if((ram.read<Byte>(n) & 0x0f) >= 0x0a) return false;
  }

  //check for valid values of each byte
  //year
  if(ram.read<Byte>(0) >= 0xa0) return false;
  //second
  if(ram.read<Byte>(5) >= 0x60) return false;
  //minute
  if(ram.read<Byte>(4) >= 0x60) return false;
  //hour
  if(ram.read<Byte>(3) >= 0x24) return false;
  //month
  if(ram.read<Byte>(1) > 0x12) return false;
  if(ram.read<Byte>(1) < 1) return false;
  //day
  if(ram.read<Byte>(2) < 1) return false;
  if(ram.read<Byte>(2) > BCD::encode(chrono::daysInMonth(BCD::decode(ram.read<Byte>(1)), BCD::decode(ram.read<Byte>(0))))) return false;

  //everything is valid
  return true;
}

