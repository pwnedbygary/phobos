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
//    it; the sizes modes tell the memory stick's free space (the stick io.cpp describes), what a save takes and what
//    saving would take, and the list mode what saves there are; deleting a save takes its folder, erasing its data
//    file alone. Names that would lead out of the save's folder, and buffers outside the program's memory, are
//    refused. The PSP encrypts the data file and writes a PARAM.SFO and icons beside it: not done yet (the files are
//    only Phobos's to read).
//  - A message: answered at once, as if the player pressed Yes (or OK), the message noted.
//  - The keyboard: each field's text accepted as it is, an empty one given the console's nickname (keyboard()).
//  - Network settings, game sharing, the web browser: cancelled, as if the player backed out.
//  - Installing a game's data to the memory stick (gamedata install, which later games show at their first start):
//    cancelled too, the game going on from the disc.
//
//Modules: games load the system's optional libraries (sound codecs, network, ...) before using them. Their functions
//are the HLE kernel's own, so loading one just marks it loaded (a second load, or unloading one that isn't, fails as
//on the PSP).

namespace {
  //the dialogs' statuses, and kinds
  enum : u32 { DialogNone = 0, DialogStarting = 1, DialogRunning = 2, DialogFinished = 3, DialogClosing = 4 };
  enum : u32 { DialogSavedata = 1, DialogMessage, DialogKeyboard, DialogNetwork, DialogSharing, DialogBrowser,
               DialogInstall };
  //errors (PPSSPP's ErrorCodes.h, from the PSP)
  constexpr u32 UtilityInvalidStatus = 0x8011'0001, UtilityInvalidSize = 0x8011'0004, UtilityWrongType = 0x8011'0005;
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
    if(kind == DialogKeyboard) answer = keyboard(dialog.parameters);
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

//The keyboard (psputility_osk.h's SceUtilityOskParams: the common part, then how many fields and where their
//SceUtilityOskData are), answered as a player accepting each field's text at once would: what the field started with
//goes into its output, its result UNCHANGED (0). A field holding nothing but spaces gets the console's nickname,
//"PSP", as a player asked for a name would type one, its result CHANGED (2): Peace Walker, asking for its player's
//name in an empty field, refused an empty answer ("at least 1 characters") and asked again for good. The text is
//UTF-16, as far as the field's room (outtextlength, its NUL among it) and limit (outtextlimit) allow: a limit of 0
//is none (taken as no characters, it cut the nickname to nothing, still saying CHANGED), and a field with no room
//gets nothing written, not even its NUL. Returns the common part's result: 0.
auto Kernel::keyboard(u32 parameters) -> u32 {
  static constexpr u32 FieldSize = 52, MostFields = 16, MostText = 1024;  //(pspsdk gives no limits: bounds of ours)
  u32 count = memory.read(4, parameters + 48), fields = memory.read(4, parameters + 52);
  for(u32 n = 0; n < std::min(count, MostFields); n++) {
    u32 field = fields + n * FieldSize;
    if(!memory.reaches(field, FieldSize)) break;
    u32 initial = memory.read(4, field + 32), room = memory.read(4, field + 36), output = memory.read(4, field + 40);
    u32 limit = memory.read(4, field + 48);
    std::vector<u16> text;
    for(u32 at = initial; initial && text.size() < MostText && memory.reaches(at, 2); at += 2) {
      u16 character = memory.read(2, at);
      if(!character) break;
      text.push_back(character);
    }
    bool blank = std::all_of(text.begin(), text.end(), [](u16 character) { return character == ' '; });
    if(blank) text = {'P', 'S', 'P'};
    u64 most = room ? room - 1 : 0;
    if(limit) most = std::min<u64>(most, limit);
    text.resize(std::min<u64>(text.size(), most));
    if(room && output && memory.reaches(output, (text.size() + 1) * 2)) {
      for(u32 at = 0; at < text.size(); at++) memory.write(2, output + at * 2, text[at]);
      memory.write(2, output + text.size() * 2, 0);
    }
    memory.write(4, field + 44, blank ? 2 : 0);
  }
  return 0;
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
//common part (48 bytes, its first word the structure's size), the mode at 48, the game's name at 60 (13 bytes), the
//save's at 76 (20), a list of save names at 96, the data file's name at 100 (13), its buffer at 116, the buffer's
//size at 120 and the data's at 124, the icons' and sound's files from 1412 (16 bytes each: buffer, its size, the
//file's size, a word), then from 1488 where to put the sizes mode's answers (free space, a save's size, what saving
//would take), and in the larger structure of firmware 2.00 on (1536 bytes), from 1524 the list mode's and at 1532
//the size mode's.
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
  case 8: {  //sizes: the memory stick's free space; a save's own size; what saving this one would take
    //(SceUtilitySavedataMsFreeInfo, SceUtilitySavedataMsDataInfo and SceUtilitySavedataUsedDataInfo: each where
    //the game wants it, or 0 for not wanted.) The save measured is the one msData names, by its own game and save
    //names: pspautotests' utility/savedata/sizes had the request's save name "ASDF", none there, and msData's "ABC",
    //and got ABC's size and 0. Without msData there's nothing to look for, and the answer is 0 (Chili Con Carnage
    //and Ace Combat X ask for the free space alone, and had been told there was no save: the first then said there
    //wasn't room for one, the second waited for good). Chosen: msData naming a save that isn't there is told so
    //(SIZES_NO_DATA), the rest still answered.
    u32 free = memory.read(4, p + 1488), data = memory.read(4, p + 1492), needed = memory.read(4, p + 1496);
    if(free) {
      memory.write(4, free, StickClusterSize);
      memory.write(4, free + 4, StickFreeClusters);
      memory.write(4, free + 8, StickFreeClusters * (StickClusterSize / 1024));
      stickText(free + 12, StickFreeClusters * (StickClusterSize / 1024));
    }
    auto usage = [&](u32 at, u64 clusters) {  //clusters, KiB, as text, then in 32 KiB steps: the same here
      u32 kilobytes = clusters * (StickClusterSize / 1024);
      memory.write(4, at, clusters);
      memory.write(4, at + 4, kilobytes);
      stickText(at + 8, kilobytes);
      memory.write(4, at + 16, kilobytes);
      stickText(at + 20, kilobytes);
    };
    u32 answer = 0;
    if(data) {  //(the game's name in 16 bytes, the save's in 20, then where its size goes)
      std::string other = memory.readString(data, 13), otherSave = memory.readString(data + 16, 20);
      bool plain = plainName(other, 13) && (otherSave.empty() || plainName(otherSave, 20));
      std::string otherFolder = plain ? savePath(other + otherSave) : std::string{};
      if(!otherFolder.empty() && fs::is_directory(otherFolder, error)) {
        usage(data + 36, savedataClusters(otherFolder));
      } else {
        answer = SavedataSizesNoData;
      }
    }
    if(needed) usage(needed, savedataNeeded(p, dataSize));
    return answer;
  }
  case 22: {  //the size mode: the memory stick's free space, and what the files the game lists would take
    //(A PspUtilitySavedataSizeInfo at 1532, in the larger structure alone: how many secure and normal files, where
    //each list of them is (a 64-bit size and a 16-byte name each), then the "sector" size, the free sectors, KiB and
    //their text, and the KiB and text still needed beyond the free space to save them new and over a save, as
    //pspsdk's neededKB and overwriteKB name them. pspautotests' utility/savedata/getsize recorded the free space
    //written, a 32 KiB cluster to a sector, and with no files listed the rest left as it was. Chosen: a listed file
    //takes its size in whole clusters, newly or over a save alike; what's needed is what they take past the free
    //space, 0 for anything a game saves on this stick, its text written only when something is needed (left as it
    //was otherwise, as the recording leaves what has nothing to say); the answer is 0, the save being there or not,
    //as a game asks before its first save.)
    u32 info = memory.read(4, p) >= 1536 ? memory.read(4, p + 1532) : 0;
    if(!info || !memory.reaches(info, 60)) return 0;
    memory.write(4, info + 16, StickClusterSize);
    memory.write(4, info + 20, StickFreeClusters);
    memory.write(4, info + 24, StickFreeClusters * (StickClusterSize / 1024));
    stickText(info + 28, StickFreeClusters * (StickClusterSize / 1024));
    u64 clusters = 0;
    u32 files = 0;
    for(u32 list : {0u, 1u}) {  //the secure files, then the normal ones
      u32 count = memory.read(4, info + list * 4), entries = memory.read(4, info + 8 + list * 4);
      for(u32 n = 0; entries && n < std::min(count, 64u) && memory.reaches(entries + n * 24, 24); n++, files++) {
        u64 size = memory.read(4, entries + n * 24) | u64(memory.read(4, entries + n * 24 + 4)) << 32;
        clusters += (size + StickClusterSize - 1) / StickClusterSize;
      }
    }
    if(files) {
      u64 kilobytes = clusters * (StickClusterSize / 1024);
      u64 free = u64(StickFreeClusters) * (StickClusterSize / 1024);
      u32 shortfall = std::min<u64>(kilobytes > free ? kilobytes - free : 0, 0x7fff'ffff);
      memory.write(4, info + 36, shortfall);
      memory.write(4, info + 48, shortfall);
      if(shortfall) {
        stickText(info + 40, shortfall);
        stickText(info + 52, shortfall);
      }
    }
    return 0;
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

//What a save on the memory stick takes, in clusters: each of its files in whole clusters, and the folder's own
//cluster (pspautotests' utility/savedata/sizes: a save of three small files, 4 clusters). As far as the host can
//tell (nothing here throws).
auto Kernel::savedataClusters(const std::string& folder) -> u64 {
  namespace fs = std::filesystem;
  u64 clusters = 1;
  std::error_code error;
  for(fs::directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
    std::error_code unread;
    u64 size = at->is_regular_file(unread) ? at->file_size(unread) : 0;
    if(!unread) clusters += (size + StickClusterSize - 1) / StickClusterSize;
  }
  return clusters;
}

//What saving the request's save would take, in clusters: its data file (dataSize bytes), the icons' and sound's
//files it carries (their sizes, at 1412 on: ICON0, ICON1, PIC1, SND0), its PARAM.SFO (a cluster) and the folder's
//own cluster. pspautotests' utility/savedata/sizes: 16 bytes of data and no other file, 3 clusters (96 KB).
auto Kernel::savedataNeeded(u32 p, u32 dataSize) -> u64 {
  auto clusters = [](u64 bytes) { return (bytes + StickClusterSize - 1) / StickClusterSize; };
  u64 total = clusters(dataSize) + 2;
  for(u32 file = 0; file < 4; file++) {
    u32 at = p + 1412 + file * 16;
    if(memory.read(4, at)) total += clusters(memory.read(4, at + 8));
  }
  return total;
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

//Installing a game's data (its parameters: the common part, then what to install where; 0x590 bytes, or 0x598 as
//later SDKs have them, and nothing else is taken). Ace Combat: Joint Assault asks about it (GetStatus) at boot, and
//goes on when it's told no install dialog was started (the wrong type, as every dialog answers). Cancelled when it
//runs.
auto Kernel::sceUtilityGamedataInstallInitStart() -> void {
  dialogDue();
  if(dialog.status != DialogNone && dialog.status != DialogClosing) return result(UtilityInvalidStatus);
  if(!memory.reaches(arg(0), 4)) return result(ErrorInvalidPointer);
  if(u32 size = memory.read(4, arg(0)); size != 0x590 && size != 0x598) return result(UtilityInvalidSize);
  dialogStart(DialogInstall);
}
auto Kernel::sceUtilityGamedataInstallGetStatus() -> void { dialogStatus(DialogInstall); }
auto Kernel::sceUtilityGamedataInstallUpdate() -> void { dialogUpdate(DialogInstall); }
auto Kernel::sceUtilityGamedataInstallShutdownStart() -> void { dialogShutdown(DialogInstall); }

//The player (or the game) stopping an install while it runs: it finishes, cancelled.
auto Kernel::sceUtilityGamedataInstallAbort() -> void {
  if(dialog.kind != DialogInstall) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogRunning) return result(UtilityInvalidStatus);
  memory.write(4, dialog.parameters + 28, DialogCancelled);
  dialog.status = DialogFinished;
  result(0);
}

//(module: psputility_modules.h's PSP_MODULE_*): network (0x100-0x106), USB devices (0x200-0x203), sound and video
//codecs (0x300-0x308), the network platform (0x400-0x402), DRM (0x500), infrared (0x600). pspsdk's list stops at
//0x307; later firmwares have one more sound and video module, 0x308 (taken here to be the MP4 library, sceMp4, whose
//functions the kernel would stand in for as for the rest). Ace Combat: Joint Assault loads it at boot and stops if
//it's refused; 0x309 still is.
static auto utilityModuleKnown(u32 module) -> bool {
  u32 group = module >> 8, index = module & 0xff;
  static constexpr u8 Counts[] = {0, 7, 4, 9, 3, 1, 1};  //how many modules in each group
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

//(sound or video module 0-7: psputility_avmodules.h's PSP_AV_MODULE_*), the older way to load them: as the modules
//0x300-0x307.
auto Kernel::sceUtilityLoadAvModule() -> void {
  if(arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x300 + arg(0);
  sceUtilityLoadModule();
}

auto Kernel::sceUtilityUnloadAvModule() -> void {
  if(arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x300 + arg(0);
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
