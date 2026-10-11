//The network libraries (sceNet, sceNetAdhoc, sceNetAdhocctl, sceNetAdhocMatching, sceNetInet, sceNetResolver,
//sceNetApctl), as a PSP whose wireless LAN switch is off has them: sceWlanGetSwitchState says off (system.cpp), the
//libraries start and stop as games expect (their init and term functions succeed: on a PSP they set up the stack
//and threads, which needs no radio), and everything that would reach another PSP or an access point fails. Games
//check the switch before offering wireless play, and start the libraries at boot regardless (Snoopy vs. the Red
//Baron, Burnout Dominator); with their starts refused, Snoopy left at once. The functions' arguments are pspsdk's
//(pspnet*.h); what a PSP returns when the radio is needed and off isn't known here, so those calls fail with
//NOT_SUPPORTED (uOFW's errors.h), which games test only for a negative number. Lists (of peers found, of sockets)
//come back empty.

namespace {
  constexpr u8 EtherAddress[6] = {0x02, 0x00, 0x00, 0x50, 0x53, 0x50};  //sceWlanGetEtherAddr's
}

//The libraries' starts and stops, and whatever else needs no radio: done.
auto Kernel::sceNetDone() -> void {
  result(0);
}

//What needs the radio: refused.
auto Kernel::sceNetUnavailable() -> void {
  result(ErrorNotSupported);
}

//(where to put it): the PSP's own address, as sceWlanGetEtherAddr gives it.
auto Kernel::sceNetGetLocalEtherAddr() -> void {
  if(!memory.copyIn(arg(0), EtherAddress, 6)) return result(ErrorInvalidPointer);
  result(0);
}

//(address, where to put its text): six bytes as "02:00:00:50:53:50".
auto Kernel::sceNetEtherNtostr() -> void {
  u8 address[6];
  if(!memory.copyOut(address, arg(0), 6)) return result(ErrorInvalidPointer);
  char text[18];
  std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", address[0], address[1], address[2], address[3],
                address[4], address[5]);
  if(!memory.copyIn(arg(1), text, sizeof(text))) return result(ErrorInvalidPointer);
  result(0);
}

//(text, where to put the address): the other way; what isn't a hexadecimal digit parts the bytes.
auto Kernel::sceNetEtherStrton() -> void {
  std::string text = memory.readString(arg(0), 32);
  u8 address[6] = {};
  u32 byte = 0, digits = 0;
  for(char c : text) {
    if(!std::isxdigit(u8(c))) {
      if(digits) byte++, digits = 0;
      continue;
    }
    if(byte >= 6) break;
    address[byte] = address[byte] << 4 | (std::isdigit(u8(c)) ? c - '0' : std::tolower(u8(c)) - 'a' + 10);
    if(++digits == 2) byte++, digits = 0;
  }
  if(!memory.copyIn(arg(1), address, 6)) return result(ErrorInvalidPointer);
  result(0);
}

//(where to put the state): disconnected (0), as nothing ever connects.
auto Kernel::sceNetAdhocctlGetState() -> void {
  if(arg(0)) memory.write(4, arg(0), 0);
  result(0);
}

//(where to put a list's length, the list): an empty list (the peers found by a scan, the PDP sockets...).
auto Kernel::sceNetEmptyList() -> void {
  if(arg(0)) memory.write(4, arg(0), 0);
  result(0);
}
