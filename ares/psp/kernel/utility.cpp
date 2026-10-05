//The system's utilities (sceUtility): the dialogs games show through the system (saving and loading, messages, the
//on-screen keyboard, network settings...), and the optional modules games load through it.
//
//Dialogs. One at a time, each through four functions: InitStart (with its parameters, a structure in the program's
//memory that also takes the answer), GetStatus, Update (called every frame while it shows) and ShutdownStart. Its
//status goes 1 (starting) for a while, 2 (running: Update does its work, then) 3 (finished: the answer is in the
//parameters), and after ShutdownStart 4 (shutting down) for a while, then 0 (none). The whiles are PPSSPP's
//(200 ms to start a save dialog, 300 for a message, 2 ms to close a save dialog...). A dialog asked about while
//another kind was started last is the wrong type. The answers given here, with nothing drawn on the screen yet:
//  - Saving and loading (savedata): a save is a folder on the memory stick, PSP/SAVEDATA/<game name><save name>,
//    holding its data file. Loading reads the file into the program's buffer, or says there's no save; saving writes
//    it; the sizes and list modes tell the memory stick's free space and what saves there are; deleting a save takes
//    its folder, erasing its data file alone. Names that would lead out of the save's folder, and buffers outside the
//    program's memory, are refused. The PSP encrypts the data file and writes a PARAM.SFO and icons beside it: not
//    done yet (the files are only Phobos's to read).
//  - A message: answered at once, as if the player pressed Yes (or OK), the message noted.
//  - The keyboard, network settings, game sharing, the web browser: cancelled, as if the player backed out.
//
//Modules: games load the system's optional libraries (sound codecs, network, ...) before using them. Their functions
//are the HLE kernel's own, so loading one just marks it loaded (a second load, or unloading one that isn't, fails as
//on the PSP).

namespace {
  //the dialogs' statuses, and kinds
  enum : u32 { DialogNone = 0, DialogStarting = 1, DialogRunning = 2, DialogFinished = 3, DialogClosing = 4 };
  enum : u32 { DialogSavedata = 1, DialogMessage, DialogKeyboard, DialogNetwork, DialogSharing, DialogBrowser };
  //errors (PPSSPP's ErrorCodes.h, from the PSP)
  constexpr u32 UtilityInvalidStatus = 0x8011'0001, UtilityWrongType = 0x8011'0005;
  constexpr u32 UtilityBadParameterID = 0x8011'0103;
  constexpr u32 ModuleBadID = 0x8011'1101, ModuleLoaded = 0x8011'1102, ModuleNotLoaded = 0x8011'1103;
  constexpr u32 SavedataLoadNoData = 0x8011'0307, SavedataReadNoData = 0x8011'0327, SavedataSizesNoData = 0x8011'03c7;
  constexpr u32 SavedataDeleteNoData = 0x8011'0347, SavedataSaveAccess = 0x8011'0385;
  //and each group's bad parameter, 8 into it (the same table)
  constexpr u32 SavedataLoadParameter = 0x8011'0308, SavedataReadParameter = 0x8011'0328;
  constexpr u32 SavedataDeleteParameter = 0x8011'0348, SavedataSaveParameter = 0x8011'0388;
  constexpr u32 SavedataSizesParameter = 0x8011'03c8;
  constexpr u32 DialogCancelled = 1;  //a dialog's result when the player backs out
  //how long a dialog takes to start and to close, in microseconds (PPSSPP's)
  auto dialogTimes(u32 kind, u32& start, u32& close) -> void {
    start = kind == DialogMessage || kind == DialogKeyboard ? 300'000 : 200'000;
    close = kind == DialogSavedata ? 2'000 : kind == DialogMessage ? 26'000 : 40'000;
    if(kind == DialogNetwork) close = 200'000;
  }
  //the memory stick's free space as saves report it: 1 GiB, in 32 KiB clusters
  constexpr u32 StickCluster = 32_KiB, StickFreeClusters = 32'768;

  //What a savedata mode says of parameters it can't take: the bad parameter of the group its other errors come from.
  auto savedataRefusal(u32 mode) -> u32 {
    switch(mode) {
    case 0: case 2: case 4: return SavedataLoadParameter;
    case 1: case 3: case 5: case 13: case 14: case 17: case 18: return SavedataSaveParameter;
    case 6: case 7: case 9: case 10: case 19: case 20: case 21: return SavedataDeleteParameter;
    case 8: return SavedataSizesParameter;
    default: return SavedataReadParameter;  //reading, and the list, files and size modes
    }
  }

  //Whether a name a program gives a save (its game's, its own, its data file's) is one plain name, ending inside its
  //field with room for its NUL: not empty, not "." or "..", with no '/', '\' or ':' (separators, or a drive, to some
  //hosts) and no control character. Each is joined to a folder on the host, where a path would lead elsewhere.
  auto plainName(const std::string& name, u32 field) -> bool {
    if(name.empty() || name.size() >= field || name == "." || name == "..") return false;
    if(name.find_first_of("/\\:") != std::string::npos) return false;
    return std::none_of(name.begin(), name.end(), [](char c) { return u8(c) < 0x20; });
  }
}

//A pending status change that's due happens.
auto Kernel::dialogDue() -> void {
  if(dialog.changeAt && cycles >= dialog.changeAt) {
    dialog.status = dialog.next;
    dialog.changeAt = 0;
  }
}

//(kind, parameters): starts a dialog, unless one is starting, running or finished and not closed.
auto Kernel::dialogStart(u32 kind) -> void {
  dialogDue();
  if(dialog.status != DialogNone && dialog.status != DialogClosing) return result(UtilityInvalidStatus);
  if(!memory.reaches(arg(0), 48)) return result(ErrorInvalidPointer);
  u32 start, close;
  dialogTimes(kind, start, close);
  dialog.kind = kind;
  dialog.parameters = arg(0);
  dialog.status = DialogStarting;
  dialog.next = DialogRunning;
  dialog.changeAt = cycles + u64(start) * (CPUFrequency / 1'000'000);
  result(0);
}

auto Kernel::dialogStatus(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  result(dialog.status);
}

//Called every frame while it shows: running, it gives its answer and finishes.
auto Kernel::dialogUpdate(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status == DialogRunning) {
    u32 answer = DialogCancelled;
    if(kind == DialogSavedata) answer = savedata(dialog.parameters);
    if(kind == DialogMessage) {
      std::string text = memory.readString(dialog.parameters + 60, 512);
      note("message dialog (answered yes): " + (text.empty() ? "error " + hexWord(memory.read(4, dialog.parameters
           + 56)) : text));
      memory.write(4, dialog.parameters + 576, 1);  //the button pressed: Yes (or OK)
      answer = 0;
    }
    memory.write(4, dialog.parameters + 28, answer);  //the common part's result
    dialog.status = DialogFinished;
  }
  result(0);
}

//Closes a finished dialog.
auto Kernel::dialogShutdown(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogFinished) return result(UtilityInvalidStatus);
  u32 start, close;
  dialogTimes(kind, start, close);
  dialog.status = DialogClosing;
  dialog.next = DialogNone;
  dialog.changeAt = cycles + u64(close) * (CPUFrequency / 1'000'000);
  result(0);
}

//A word as eight hexadecimal digits.
auto Kernel::hexWord(u32 value) -> std::string {
  char text[12];
  std::snprintf(text, sizeof(text), "%08x", value);
  return text;
}

//Where a save is on the host: its folder, ms0:/PSP/SAVEDATA/<folder> (the game's name and the save's), or given a
//data file's name, that file in it; nothing if there's no memory stick. resolve() keeps a path inside the memory
//stick; a save's must also be where a save belongs, links followed: the folder one of SAVEDATA's own (never SAVEDATA
//itself, nor anything above it), the file one of the folder's own. A path leading anywhere else is nothing too.
auto Kernel::savePath(const std::string& folder, const std::string& file) -> std::string {
  namespace fs = std::filesystem;
  std::string saves, host, normalized;
  if(resolve("ms0:/PSP/SAVEDATA", saves, normalized)) return {};
  if(resolve("ms0:/PSP/SAVEDATA/" + folder, host, normalized) || fs::path(host).parent_path() != saves) return {};
  if(file.empty()) return host;
  std::string inside = host;
  if(resolve("ms0:/PSP/SAVEDATA/" + folder + "/" + file, host, normalized)) return {};
  return fs::path(host).parent_path() == inside ? host : std::string{};
}

//Does what a SceUtilitySavedataParam (psputility_savedata.h) asks, by its mode; the dialog's result. The layout: the
//common part (48 bytes), the mode at 48, the game's name at 60 (13 bytes), the save's at 76 (20), a list of save
//names at 96, the data file's name at 100 (13), its buffer at 116, the buffer's size at 120 and the data's at 124,
//then from 1488 where to put the sizes mode's answers (free space, the save's size, what saving would take), and from
//1524 the list mode's.
//
//The names become a folder and a file on the host: each must be a plain name (plainName(); the game's can't be left
//out, the save's can), and the paths where a save belongs (savePath()). The buffer must be in the program's memory as
//far as what's copied in or out, which memory's size bounds: a load reads no more than the buffer takes. What isn't
//so is refused as a bad parameter, before anything is made, read or deleted.
auto Kernel::savedata(u32 p) -> u32 {
  namespace fs = std::filesystem;
  u32 mode = memory.read(4, p + 48);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  std::string fileName = memory.readString(p + 100, 13);
  u32 buffer = memory.read(4, p + 116), bufferSize = memory.read(4, p + 120), dataSize = memory.read(4, p + 124);
  u32 refused = savedataRefusal(mode);
  std::error_code error;
  if(mode == 11) return savedataList(p, game);  //(the game's name only picks the folders listed)
  if((mode == 4 || mode == 5) && save.empty() && memory.read(4, p + 96)) {  //from a list: its first name
    save = memory.readString(memory.read(4, p + 96), 20);
    if(save.empty()) return refused;
  }
  if(!plainName(game, 13) || (!save.empty() && !plainName(save, 20))) return refused;
  bool filed = mode <= 5 || (mode >= 13 && mode <= 20);  //the modes that read, write or erase the data file
  if(filed && !fileName.empty() && !plainName(fileName, 13)) return refused;
  std::string folder = savePath(game + save);
  std::string file = filed && !fileName.empty() ? savePath(game + save, fileName) : std::string{};
  bool exists = !folder.empty() && fs::is_directory(folder, error);
  switch(mode) {
  case 0: case 2: case 4: case 15: case 16: {  //load (auto, chosen, from a list), read (secure or not)
    u32 none = mode >= 15 ? SavedataReadNoData : SavedataLoadNoData;
    if(!exists || file.empty() || !fs::is_regular_file(file, error)) return none;
    u64 size = fs::file_size(file, error);
    if(error) return none;
    u32 count = std::min<u64>(size, bufferSize);
    if(count && !memory.reaches(buffer, count)) return refused;
    std::vector<u8> bytes(count);
    std::ifstream stream(file, std::ios::binary);
    stream.read((char*)bytes.data(), count);
    count = stream.gcount();
    memory.copyIn(buffer, bytes.data(), count);
    memory.write(4, p + 124, count);  //what it read
    return 0;
  }
  case 1: case 3: case 5: case 13: case 14: case 17: case 18: {  //save (auto, chosen, to a list), make, write
    u32 size = std::min(dataSize, bufferSize);
    if(folder.empty() || (!fileName.empty() && file.empty())) return SavedataSaveAccess;
    if(!fileName.empty() && size && !memory.reaches(buffer, size)) return refused;
    fs::create_directories(folder, error);
    if(fileName.empty()) return 0;
    std::vector<u8> bytes(size);
    memory.copyOut(bytes.data(), buffer, size);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write((const char*)bytes.data(), size);
    return stream ? 0 : SavedataSaveAccess;
  }
  case 8: {  //sizes: the memory stick's free space; the save's own size, if there's one; what saving takes
    u32 free = memory.read(4, p + 1488), used = memory.read(4, p + 1492), needed = memory.read(4, p + 1496);
    if(free) {
      memory.write(4, free, StickCluster);
      memory.write(4, free + 4, StickFreeClusters);
      memory.write(4, free + 8, StickCluster / 1024 * StickFreeClusters);
      memory.copyIn(free + 12, "1 GB\0\0\0", 8);
    }
    u64 bytes = 0;
    if(exists) {  //its files' sizes, as far as the host can tell them (nothing here throws)
      for(fs::directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
        std::error_code unread;
        u64 size = at->is_regular_file(unread) ? at->file_size(unread) : 0;
        if(!unread) bytes += size;
      }
    }
    auto usage = [&](u32 at, u64 size) {  //clusters, KiB, as text, then in 32 KiB steps: the same here
      u32 clusters = (size + StickCluster - 1) / StickCluster, kilobytes = clusters * (StickCluster / 1024);
      std::string text = std::to_string(kilobytes) + " KB";
      memory.write(4, at, clusters);
      memory.write(4, at + 4, kilobytes);
      memory.copyIn(at + 8, text.c_str(), std::min<u32>(text.size() + 1, 8));
      memory.write(4, at + 16, kilobytes);
      memory.copyIn(at + 20, text.c_str(), std::min<u32>(text.size() + 1, 8));
    };
    if(used) {
      memory.copyIn(used, game.c_str(), std::min<u32>(game.size() + 1, 13));
      memory.copyIn(used + 16, save.c_str(), std::min<u32>(save.size() + 1, 20));
      usage(used + 36, bytes);
    }
    if(needed) usage(needed, std::max(dataSize, 1u));
    return exists ? 0 : SavedataSizesNoData;
  }
  case 6: case 7: case 9: case 10: case 21: {  //deleting a save: its folder goes, with all it holds
    if(!exists) return SavedataDeleteNoData;
    fs::remove_all(folder, error);
    return 0;
  }
  case 19: case 20: {  //erasing (secure or not): the data file named goes, and nothing else (none named, nothing)
    if(!exists) return SavedataDeleteNoData;
    if(fileName.empty()) return 0;
    if(file.empty() || !fs::is_regular_file(file, error)) return SavedataDeleteNoData;
    fs::remove(file, error);
    return 0;
  }
  default:  //the files mode, the sizes of others: nothing to tell
    return exists ? 0 : SavedataReadNoData;
  }
}

//The list mode (savedata()'s 11): the game's saves, its folders in SAVEDATA whose names start with the game's, into
//a SceUtilitySavedataIdListInfo (from 1524: the most to list, how many were listed, the entries). Only the names are
//read, as far as the host can tell them (nothing here throws).
auto Kernel::savedataList(u32 p, const std::string& game) -> u32 {
  namespace fs = std::filesystem;
  u32 list = memory.read(4, p + 1524);
  if(!list) return 0;
  u32 most = memory.read(4, list), entries = memory.read(4, list + 8), count = 0;
  std::string saves, normalized;
  if(!resolve("ms0:/PSP/SAVEDATA", saves, normalized)) {
    std::vector<std::string> names;
    std::error_code error;
    for(fs::directory_iterator at(saves, error), end; !error && at != end; at.increment(error)) {
      std::error_code unread;
      auto name = at->path().filename().string();
      if(at->is_directory(unread) && name.compare(0, game.size(), game) == 0) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    for(auto& name : names) {
      if(count == most) break;
      u32 at = entries + count++ * 72;  //its mode, three dates (16 bytes each), then its name (20)
      memory.fill(at, 0, 72);
      memory.write(4, at, 0x11ff);    //a folder
      memory.copyIn(at + 52, name.c_str() + game.size(), std::min<u32>(name.size() - game.size() + 1, 20));
    }
  }
  memory.write(4, list + 4, count);
  return 0;
}

auto Kernel::sceUtilitySavedataInitStart() -> void { dialogStart(DialogSavedata); }
auto Kernel::sceUtilitySavedataGetStatus() -> void { dialogStatus(DialogSavedata); }
auto Kernel::sceUtilitySavedataUpdate() -> void { dialogUpdate(DialogSavedata); }
auto Kernel::sceUtilitySavedataShutdownStart() -> void { dialogShutdown(DialogSavedata); }
auto Kernel::sceUtilityMsgDialogInitStart() -> void { dialogStart(DialogMessage); }
auto Kernel::sceUtilityMsgDialogGetStatus() -> void { dialogStatus(DialogMessage); }
auto Kernel::sceUtilityMsgDialogUpdate() -> void { dialogUpdate(DialogMessage); }
auto Kernel::sceUtilityMsgDialogShutdownStart() -> void { dialogShutdown(DialogMessage); }
auto Kernel::sceUtilityOskInitStart() -> void { dialogStart(DialogKeyboard); }
auto Kernel::sceUtilityOskGetStatus() -> void { dialogStatus(DialogKeyboard); }
auto Kernel::sceUtilityOskUpdate() -> void { dialogUpdate(DialogKeyboard); }
auto Kernel::sceUtilityOskShutdownStart() -> void { dialogShutdown(DialogKeyboard); }
auto Kernel::sceUtilityNetconfInitStart() -> void { dialogStart(DialogNetwork); }
auto Kernel::sceUtilityNetconfGetStatus() -> void { dialogStatus(DialogNetwork); }
auto Kernel::sceUtilityNetconfUpdate() -> void { dialogUpdate(DialogNetwork); }
auto Kernel::sceUtilityNetconfShutdownStart() -> void { dialogShutdown(DialogNetwork); }
auto Kernel::sceUtilityGameSharingInitStart() -> void { dialogStart(DialogSharing); }
auto Kernel::sceUtilityGameSharingGetStatus() -> void { dialogStatus(DialogSharing); }
auto Kernel::sceUtilityGameSharingUpdate() -> void { dialogUpdate(DialogSharing); }
auto Kernel::sceUtilityGameSharingShutdownStart() -> void { dialogShutdown(DialogSharing); }
auto Kernel::sceUtilityHtmlViewerInitStart() -> void { dialogStart(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerGetStatus() -> void { dialogStatus(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerUpdate() -> void { dialogUpdate(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerShutdownStart() -> void { dialogShutdown(DialogBrowser); }

//(module: psputility_modules.h's PSP_MODULE_*): network (0x100-0x106), USB devices (0x200-0x203), sound and video
//codecs (0x300-0x307), the network platform (0x400-0x402), DRM (0x500), infrared (0x600).
static auto utilityModuleKnown(u32 module) -> bool {
  u32 group = module >> 8, index = module & 0xff;
  static constexpr u8 Counts[] = {0, 7, 4, 8, 3, 1, 1};  //how many modules in each group
  return group >= 1 && group <= 6 && index < Counts[group];
}

auto Kernel::sceUtilityLoadModule() -> void {
  u32 module = arg(0);
  if(!utilityModuleKnown(module)) return result(ModuleBadID);
  if(std::count(utilityModules.begin(), utilityModules.end(), module)) return result(ModuleLoaded);
  utilityModules.push_back(module);
  result(0);
}

auto Kernel::sceUtilityUnloadModule() -> void {
  u32 module = arg(0);
  if(!utilityModuleKnown(module)) return result(ModuleBadID);
  auto found = std::find(utilityModules.begin(), utilityModules.end(), module);
  if(found == utilityModules.end()) return result(ModuleNotLoaded);
  utilityModules.erase(found);
  result(0);
}

//(network module 1-7: psputility_netmodules.h's PSP_NET_MODULE_*), the older way to load them: as the modules
//0x100-0x106.
auto Kernel::sceUtilityLoadNetModule() -> void {
  if(arg(0) < 1 || arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x100 + arg(0) - 1;
  sceUtilityLoadModule();
}

auto Kernel::sceUtilityUnloadNetModule() -> void {
  if(arg(0) < 1 || arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x100 + arg(0) - 1;
  sceUtilityUnloadModule();
}

//(which, where, its size): the system's text settings: the player's nickname (1) is "PSP". Others aren't known.
auto Kernel::sceUtilityGetSystemParamString() -> void {
  if(arg(0) != 1) return result(UtilityBadParameterID);
  const char nickname[] = "PSP";
  if(arg(2) < sizeof(nickname)) return result(0x8011'0102);  //the string too long for its room
  memory.copyIn(arg(1), nickname, sizeof(nickname));
  result(0);
}

//(which, text): setting them is accepted and forgotten: the settings stay the system's own.
auto Kernel::sceUtilitySetSystemParamString() -> void {
  result(0);
}
