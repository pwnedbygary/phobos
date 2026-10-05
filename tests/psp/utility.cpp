//The system's utilities (ares/psp/kernel/utility.cpp): a dialog's statuses and their timing, saves made, loaded,
//sized, listed and deleted in a memory stick folder, a message answered yes, other dialogs cancelled, the wrong
//type and the wrong status refused; optional modules loaded and unloaded; the nickname. Called directly, the clock
//moved on by hand.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 Parameters = 0x0892'0000, Buffer = 0x0893'0000, Free = 0x0894'0000, Used = 0x0894'0100;
constexpr u32 Needed = 0x0894'0200, List = 0x0894'0300, Entries = 0x0894'0400;

//A SceUtilitySavedataParam for (mode, game, save, file, the data's size), its answers' structures pointed at.
auto saveParameters(KernelMachine& m, u32 mode, const char* game, const char* save, u32 dataSize) -> void {
  m.system.memory.fill(Parameters, 0, 1536);
  m.system.memory.write(4, Parameters, 1536);
  m.system.memory.write(4, Parameters + 48, mode);
  m.system.memory.copyIn(Parameters + 60, game, strlen(game) + 1);
  m.system.memory.copyIn(Parameters + 76, save, strlen(save) + 1);
  m.system.memory.copyIn(Parameters + 100, "DATA.BIN", 9);
  m.system.memory.write(4, Parameters + 116, Buffer);
  m.system.memory.write(4, Parameters + 120, 0x1000);
  m.system.memory.write(4, Parameters + 124, dataSize);
  m.system.memory.write(4, Parameters + 1488, Free);
  m.system.memory.write(4, Parameters + 1492, Used);
  m.system.memory.write(4, Parameters + 1496, Needed);
  m.system.memory.write(4, Parameters + 1524, List);
  m.system.memory.write(4, List, 10);
  m.system.memory.write(4, List + 8, Entries);
}

//A whole savedata dialog: started, running after 200 ms, its work done by an update, closed 2 ms after it's shut
//down; its result.
auto runSave(KernelMachine& m) -> u32 {
  u64 millisecond = Kernel::CPUFrequency / 1000;
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 1);
  m.kernel.cycles += 199 * millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 1);
  m.kernel.cycles += millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 2);
  CHECK(m.call("sceUtilitySavedataUpdate", {1}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 3);
  CHECK(m.call("sceUtilitySavedataShutdownStart", {}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 4);
  m.kernel.cycles += 2 * millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0);
  return m.system.memory.read(4, Parameters + 28);
}
}

//Saves: none to load at first; one saved (its data file in PSP/SAVEDATA/<game><save>), loaded back, sized (the
//memory stick's free space, its size, what saving would take), listed, deleted, and none again.
static auto savedata() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0x8011'0005);  //no dialog yet: the wrong type
  saveParameters(m, 0, "ULUS99999", "SLOT0", 0);  //autoload
  CHECK(runSave(m), 0x8011'0307);
  m.system.memory.copyIn(Buffer, "saved bytes", 11);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 11);  //autosave
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN") == "saved bytes", true);
  m.system.memory.fill(Buffer, 0, 16);
  saveParameters(m, 2, "ULUS99999", "SLOT0", 0);  //load
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 16) == "saved bytes", true);
  CHECK(m.system.memory.read(4, Parameters + 124), 11);
  saveParameters(m, 8, "ULUS99999", "SLOT0", 40000);  //sizes
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, Free), 32_KiB);
  CHECK(m.system.memory.read(4, Free + 4), 32768);
  CHECK(m.system.memory.read(4, Free + 8), 1024 * 1024);
  CHECK(m.system.memory.readString(Free + 12, 8) == "1 GB", true);
  CHECK(m.system.memory.readString(Used + 16, 20) == "SLOT0", true);
  CHECK(m.system.memory.read(4, Used + 36), 1);    //one cluster used
  CHECK(m.system.memory.read(4, Needed), 2);       //40000 bytes take two
  CHECK(m.system.memory.read(4, Needed + 4), 64);
  saveParameters(m, 11, "ULUS99999", "", 0);  //list
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, List + 4), 1);
  CHECK(m.system.memory.readString(Entries + 52, 20) == "SLOT0", true);
  saveParameters(m, 10, "ULUS99999", "SLOT0", 0);  //delete
  CHECK(runSave(m), 0);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
  saveParameters(m, 16, "ULUS99999", "SLOT0", 0);  //read: none
  CHECK(runSave(m), 0x8011'0327);
  saveParameters(m, 8, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0x8011'03c7);
  saveParameters(m, 10, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0x8011'0347);
}

//The other dialogs: a message answered yes at once (noted); the keyboard and network settings cancelled; one at a
//time; asked about as another kind, the wrong type; shut down before finishing, the wrong status.
static auto dialogs() -> void {
  KernelMachine m;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  m.system.memory.fill(Parameters, 0, 580);
  m.system.memory.copyIn(Parameters + 60, "Save failed.", 13);
  CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0);
  CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0x8011'0001);  //one at a time
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0x8011'0001);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0x8011'0005);
  CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0x8011'0001);  //not finished
  m.kernel.cycles += 300 * millisecond;
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 2);
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0);
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 3);
  CHECK(m.system.memory.read(4, Parameters + 576), 1);  //yes
  CHECK(m.system.memory.read(4, Parameters + 28), 0);
  CHECK(m.notes.size() == 1 && m.notes[0] == "message dialog (answered yes): Save failed.", true);
  CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0);
  m.kernel.cycles += 26 * millisecond;
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 0);
  for(auto [start, update, status] : {std::tuple{"sceUtilityNetconfInitStart", "sceUtilityNetconfUpdate",
        "sceUtilityNetconfGetStatus"}, std::tuple{"sceUtilityOskInitStart", "sceUtilityOskUpdate",
        "sceUtilityOskGetStatus"}}) {
    m.system.memory.fill(Parameters, 0, 580);
    CHECK(m.call(start, {Parameters}), 0);
    m.kernel.cycles += 300 * millisecond;
    CHECK(m.call(update, {1}), 0);
    CHECK(m.call(status, {}), 3);
    CHECK(m.system.memory.read(4, Parameters + 28), 1);  //cancelled
    m.kernel.dialog.status = 0;  //closed (as if shut down)
  }
}

//Optional modules: loaded once (again: already loaded), unloaded once (again: not loaded), numbers that aren't a
//module refused, the older network numbers standing for 0x100-0x106; the nickname.
static auto modules() -> void {
  KernelMachine m;
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0x8011'1102);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0x8011'1103);
  CHECK(m.call("sceUtilityLoadModule", {0x308}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadModule", {0x700}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x100}), 0x8011'1102);  //the same module
  CHECK(m.call("sceUtilityUnloadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadNetModule", {8}), 0x8011'1101);
  CHECK(m.call("sceUtilityGetSystemParamString", {1, Buffer, 128}), 0);
  CHECK(m.system.memory.readString(Buffer, 128) == "PSP", true);
  CHECK(m.call("sceUtilityGetSystemParamString", {2, Buffer, 128}), 0x8011'0103);
}

auto utilityTests() -> Tests {
  return {{"utility savedata", savedata}, {"utility dialogs", dialogs}, {"utility modules", modules}};
}

}
