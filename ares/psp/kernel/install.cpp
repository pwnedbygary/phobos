//The game data install dialog (sceUtilityGamedataInstall*: docs/psp-core.md, part 99). A game that plays faster from a
//copy of some of its disc's data on the memory stick starts it with its parameters (utility.cpp's dialogStart(),
//0x590 or 0x598 bytes): the common part, then at 48 the mode (1: the PSP shows its own progress screen; 0, or less:
//nothing, the game draws its own), at 52 the game's name (13 bytes), at 68 the data's (20), at 88 the titles the
//install's PARAM.SFO takes, as a save's sfoParam (the title in 128 bytes, the data's own title in 128 at 216, its
//detail in 1024 at 344, at 1368 the parental level), and at 1372 its progress, 0 to 100, which it keeps current as it
//copies. The layout is PPSSPP's notes', and Kingdom Hearts Birth by Sleep Final Mix's code fills and reads it so: mode
//0, ULJM05775 and DATA, "Install Data", "Deleting this will cause load times to increase.", parental level 1, its
//progress read for its own install screen, then only the result. What follows 1372 is unknown, left as the game has
//it.
//
//What it does, as PPSSPP's notes describe the PSP's: every file of the disc's PSP_GAME/INSDIR goes into the memory
//stick's PSP/SAVEDATA/<game><data>, with a PARAM.SFO describing it as a save's does (TITLE, SAVEDATA_TITLE,
//SAVEDATA_DETAIL and PARENTAL_LEVEL from the parameters, CATEGORY "MS", SAVEDATA_DIRECTORY the folder's name; and,
//chosen, as every save Phobos writes has them, SAVEDATA_FILE_LIST naming no secure file and SAVEDATA_PARAMS of
//zeros). Kingdom Hearts' INSDIR holds the install's icons alone (ICON0.PNG, PIC1.PNG), the game copying its data into
//the folder itself afterwards; Gunhound EX's holds all of its 460 MB, 797 files. A folder of that name already there
//(an install made before, which the games ask about first) is written over as the PSP's dialog writes it, PPSSPP's
//notes describe, and Gunhound EX's own install again shows: INSDIR's files and PARAM.SFO replaced (its keys the
//request doesn't set kept), anything else in it left as it was; no clean-room recording covers it. Nothing else on
//the memory stick is touched.
//
//Phobos copies a piece at each Update (InstallBytesPerUpdate, a fixed amount, so a run takes as many Updates every
//time; Gunhound EX's install takes about 8 seconds at 60 a second), into a folder of its own first,
//<the memory stick>/.phobos-install/<game><data> (InstallCopies: io.cpp keeps it from the programs' sight and reach),
//which becomes the save's folder (or whose files go into the one there, PARAM.SFO last) once every file and PARAM.SFO
//are written. So Phobos stopped part way (killed, the machine reset, the game aborting the dialog) leaves the saves as
//they were, never a half folder a game would take for its install. As one install runs at a time, whatever is in that
//folder when one starts, or when the memory stick is put in at power on (mount()), is a copy left so, and is taken
//away: a phone that killed Phobos part way through a 1.5 GB install gets its room back at the next boot. The copy
//isn't state: loading a state takes the copy's folder away (and nothing else), and an install running in the state
//starts again at its next Update.
//
//Its answers, the common part's result at 28: 0, installed. 1, cancelled: the game's Abort, and (chosen) a disc with
//nothing to install, as the dialog answered every install before part 99. 0x80111901, no memory stick (PPSSPP's
//notes' MEMSTICK_REMOVED). 0x80111902, not enough free space: chosen, from the pattern of the codes known (the save
//group's 0x80110382 to 0x80110385 are removed, no space, protected and access, as 0x80111901, PPSSPP's notes'
//0x80111903 WRITE_PROTECTED and 0x80111904 are removed, protected and access). 0x80111904, the memory stick couldn't be
//written (what PSPs show for Kingdom Hearts' install failing on a bad memory stick, in players' reports: "The Memory
//Stick could not be accessed (80111904)"); chosen too for names that would lead out of SAVEDATA, a file where the
//folder goes, a copy left before that can't be taken away, and the disc failing to read. 0x80111908, a mode the PSP
//doesn't take, 2 or more (PPSSPP's notes). The free space is the memory stick's as every function tells it (io.cpp's
//StickFreeClusters), the install taking each file in whole clusters, and a cluster for its PARAM.SFO and one for the
//folder, as savedata's sizes count a save. Where the host itself hasn't room for the copy and 64 MiB more (a phone's
//storage nearly full), the same answer, before anything is written; and the same where a write fails with the host
//short of that room (filled meanwhile), the copy given up and its room given back.

namespace {
  constexpr u32 InstallRemoved = 0x8011'1901, InstallNoSpace = 0x8011'1902, InstallAccess = 0x8011'1904;
  constexpr u32 InstallBadMode = 0x8011'1908;
  constexpr s32 InstallShowsProgress = 1;   //the mode the PSP shows its progress screen in
  constexpr u64 InstallHostRoom = 64_MiB;   //what an install leaves free on the host, at least
  constexpr u32 InstallIconMost = 1_MiB;    //(INSDIR's ICON0.PNG shown by a front end: anything larger isn't one)

  auto installAnswerName(u32 answer) -> const char* {
    switch(answer) {
    case 0: return "installed";
    case 1: return "cancelled";
    case InstallRemoved: return "no memory stick";
    case InstallNoSpace: return "no space";
    case InstallAccess: return "access";
    case InstallBadMode: return "bad mode";
    }
    return "?";
  }
}

//A line for a host tracing the games' saves (the runner's --trace-savedata).
auto Kernel::installTrace(const std::string& text) -> void {
  if(savedataTrace) savedataTrace("gamedata install: " + text);
}

//The copies' folder at the top of the memory stick in the host folder `stick` taken away with all it holds: copies
//installs left (Phobos stopped part way), as no install is running when this is asked. A link put in its place is
//taken away itself, never followed. Whether nothing of it is left.
auto Kernel::installLeftovers(const std::string& stick) -> bool {
  namespace fs = std::filesystem;
  std::error_code error;
  fs::remove_all(fs::path(stick) / InstallCopies, error);
  return !error;
}

//Why a write to the copy failed: the host short of room (the 64 MiB an install leaves free), so no space, as a full
//memory stick would answer; anything else, the memory stick couldn't be written.
auto Kernel::installWriteFailed() -> u32 {
  namespace fs = std::filesystem;
  std::error_code error;
  auto space = fs::space(fs::path(install.partial).parent_path(), error);
  return !error && space.available < InstallHostRoom ? InstallNoSpace : InstallAccess;
}

//Whether the copy's folder, and the copies' folder it's in, are still the folders installBegin() made on the stick,
//not links a host put in their place: through one, the copy's files would be written, moved into the saves, or taken
//away off the stick.
auto Kernel::installFolderIntact() -> bool {
  namespace fs = std::filesystem;
  std::error_code error;
  fs::path folder = install.partial;
  return fs::is_directory(fs::symlink_status(folder.parent_path(), error))
      && fs::is_directory(fs::symlink_status(folder, error));
}

//(at an Update while the dialog runs, from dialogUpdate()) The copy begun, or carried on a piece, its progress
//written; once every file is there, made the save's folder, and the dialog finished.
auto Kernel::installUpdate() -> void {
  if(!install.started) {
    if(u32 answer = installBegin()) return installFinish(answer);
  }
  if(u32 answer = installStep()) return installFinish(answer);
  u32 progress = install.total ? u32(install.copied * 100 / install.total) : 100;
  memory.write(4, dialog.parameters + 1372, progress);
  if(install.file < install.files.size()) return installShow();
  installFinish(installCommit());
}

//The copy begun: INSDIR's files listed (a disc with none answered first, as before part 99), the request checked (a
//file where the folder goes found before anything is copied), the files measured against the free space, and the
//copies' folder emptied of every copy left in it (installLeftovers()), or the install given up if it can't be. 0, or
//the answer the dialog finishes with.
auto Kernel::installBegin() -> u32 {
  namespace fs = std::filesystem;
  u32 p = dialog.parameters;
  install = {};
  install.started = true;
  s32 mode = memory.read(4, p + 48);
  std::string game = memory.readString(p + 52, 13), data = memory.readString(p + 68, 20);
  install.folder = game + data;
  std::string request = "mode " + std::to_string(mode) + ", size " + std::to_string(memory.read(4, p)) + ", game "
                      + traceName(game) + ", data " + traceName(data) + ", title "
                      + traceName(memory.readString(p + 216, 128));
  auto refused = [&](u32 answer) {
    installTrace(request + " -> 0x" + hexWord(answer) + " (" + installAnswerName(answer) + ")");
    return answer;
  };
  std::string root, normalized, target;
  std::error_code error;
  //(names that are plain, as a save's must be, and a folder's not ending in '.' or ' ': FAT and Windows drop those,
  //so "DATA." would go into DATA's folder there and be a folder of its own elsewhere. The savedata utility's names are
  //left as they were, a save made so still loading.)
  bool plain = plainName(game, 13) && (data.empty() || plainName(data, 20));
  plain = plain && install.folder.back() != '.' && install.folder.back() != ' ';
  if(plain && devices.count("ms0")) target = savePath(install.folder);
  //INSDIR's files (folders in it aren't copied), from the disc image, or from the host folder standing for the disc
  //(sorted, as the disc's are)
  std::string insdir = "disc0:/PSP_GAME/INSDIR";
  if(onDisc(insdir)) {
    Disc::Entry entry;
    std::vector<std::string> names;
    if(!resolveOnDisc(insdir, entry, normalized, names) && entry.folder) {
      for(auto& found : disc->list(entry)) {
        if(!found.folder) install.files.push_back({found.name, found.size, found.sector, {}});
      }
    }
  } else if(std::string host; !resolve(insdir, host, normalized) && fs::is_directory(host, error)) {
    for(fs::directory_iterator at(host, error), end; !error && at != end; at.increment(error)) {
      std::error_code unread;
      if(!at->is_regular_file(unread)) continue;
      u64 size = at->file_size(unread);
      if(!unread) install.files.push_back({at->path().filename().string(), size, 0, at->path().string()});
    }
    std::sort(install.files.begin(), install.files.end(), [](auto& a, auto& b) { return a.name < b.name; });
  }
  u64 clusters = 2;  //(its PARAM.SFO, and the folder)
  for(auto& file : install.files) {
    plain = plain && plainName(file.name, 256);
    install.total += file.size;
    clusters += (file.size + StickClusterSize - 1) / StickClusterSize;
  }
  bool there = !target.empty() && fs::is_directory(target, error);
  request += ", " + std::to_string(install.files.size()) + " files " + std::to_string(install.total) + " bytes from "
           + insdir + ", " + traceName(install.folder) + (there ? " there" : " not there");
  if(install.files.empty()) return refused(DialogCancelled);
  if(mode >= 2) return refused(InstallBadMode);
  if(!devices.count("ms0")) return refused(InstallRemoved);
  if(!memory.reaches(p, 0x590) || !plain || target.empty() || resolve("ms0:/", root, normalized)) {
    return refused(InstallAccess);
  }
  if(fs::exists(fs::symlink_status(target, error)) && !there) return refused(InstallAccess);
  if(clusters > StickFreeClusters) return refused(InstallNoSpace);
  auto space = fs::space(root, error);
  if(!error && space.available < install.total + InstallHostRoom) return refused(InstallNoSpace);
  //(its own folder, a real one, so what's taken away is the copies' alone)
  auto copies = fs::path(root) / InstallCopies;
  if(fs::is_symlink(copies, error) || !installLeftovers(root)) return refused(InstallAccess);
  install.partial = (copies / install.folder).string();
  fs::create_directories(install.partial, error);
  if(error || !fs::is_directory(install.partial, error)) return refused(InstallAccess);
  if(frontDialogs && mode == InstallShowsProgress) {
    for(auto& file : install.files) {
      if(!sameName(file.name, "ICON0.PNG") || !file.size || file.size > InstallIconMost) continue;
      install.icon.resize(file.size);
      if(!installRead(file, 0, file.size, install.icon.data())) install.icon.clear();
    }
  }
  installTrace(request);
  return 0;
}

//A piece of the copy: up to InstallBytesPerUpdate bytes, the next of each file in turn, onto the end of its copy (an
//empty file made as it's reached). 0, or the answer the dialog finishes with (for a write that fails,
//installWriteFailed()'s). The copy must be as the install left it, its folders real ones (installFolderIntact()),
//nothing there before a file's first piece and a file of its own as long as what's been copied after: one the host
//took away, cut short or put a link in place of meanwhile (a tool keeping the stick in step with another device's,
//say) would otherwise be made again or written on at its end, a short install answered 0, or be written through the
//link. It gives the install up instead (0x80111904), as a write failing does.
auto Kernel::installStep() -> u32 {
  namespace fs = std::filesystem;
  if(!installFolderIntact()) return InstallAccess;
  std::vector<u8> bytes;
  for(u64 left = InstallBytesPerUpdate; install.file < install.files.size() && left;) {
    auto& file = install.files[install.file];
    u64 count = std::min(left, file.size - install.at);
    bytes.resize(count);
    if(count && !installRead(file, install.at, count, bytes.data())) return InstallAccess;
    auto path = fs::path(install.partial) / file.name;
    std::error_code error;
    auto status = fs::symlink_status(path, error);
    if(install.at ? !fs::is_regular_file(status) || fs::file_size(path, error) != install.at : fs::exists(status)) {
      return InstallAccess;
    }
    auto mode = std::ios::binary | (install.at ? std::ios::app : std::ios::trunc);
    std::ofstream stream(path, mode);
    stream.write((const char*)bytes.data(), count);
    stream.close();
    if(!stream) return installWriteFailed();
    install.at += count, install.copied += count, left -= count;
    if(install.at == file.size) install.file++, install.at = 0;
  }
  return 0;
}

//Bytes of one of INSDIR's files, from the disc image or its host file.
auto Kernel::installRead(const Install::File& file, u64 at, u64 size, u8* data) -> bool {
  if(file.host.empty()) return disc && disc->read(u64(file.sector) * Disc::SectorSize + at, size, data);
  std::ifstream stream(file.host, std::ios::binary);
  return stream.seekg(at) && stream.read((char*)data, size);
}

//Every file copied: the install's PARAM.SFO written beside them (from INSDIR's own if it had one, else the one in the
//folder there, else afresh; a broken one afresh), then the copy made the save's folder, or each of its files moved into
//the one there over the file of its name, PARAM.SFO last. 0, or the answer the dialog finishes with. (What's at the
//save's folder is looked at again here: installBegin() looked before the copy, which a host may have changed since.
//So are the copy's folders, real ones still, and each file of the copy, a file of its own as long as the disc's, as a
//host may have taken one away or cut it short after its last piece: 0x80111904 rather than a short install.)
auto Kernel::installCommit() -> u32 {
  namespace fs = std::filesystem;
  u32 p = dialog.parameters;
  std::error_code error;
  if(!installFolderIntact()) return InstallAccess;
  for(auto& file : install.files) {
    auto path = fs::path(install.partial) / file.name;
    if(!fs::is_regular_file(fs::symlink_status(path, error)) || fs::file_size(path, error) != file.size) {
      return InstallAccess;
    }
  }
  std::string target = savePath(install.folder);
  if(target.empty()) return InstallAccess;
  bool there = fs::exists(fs::symlink_status(target, error));
  if(there && !fs::is_directory(target, error)) return InstallAccess;
  SaveSFO sfo;
  bool broken = false;
  std::string written = (fs::path(install.partial) / "PARAM.SFO").string();
  if(!sfo.read(written, broken) && there) sfo.read(savePath(install.folder, "PARAM.SFO"), broken);
  auto describe = [&] {
    sfo.setText("TITLE", memory.readString(p + 88, 128), 128);
    sfo.setText("SAVEDATA_TITLE", memory.readString(p + 216, 128), 128);
    sfo.setText("SAVEDATA_DETAIL", memory.readString(p + 344, 1024), 1024);
    sfo.setNumber("PARENTAL_LEVEL", memory.read(1, p + 1368));
    sfo.setText("CATEGORY", "MS", 4);
    sfo.setText("SAVEDATA_DIRECTORY", install.folder, 64);
    sfo.fileList();
    if(!sfo.values.count("SAVEDATA_PARAMS")) sfo.setBytes("SAVEDATA_PARAMS", std::vector<u8>(128));
  };
  if(broken) sfo.values.clear();
  describe();
  if(sfo.size() > 64_KiB) sfo.values.clear(), describe();
  if(!sfo.write(written)) return installWriteFailed();
  fs::create_directories(fs::path(target).parent_path(), error);
  if(!there) {
    fs::rename(install.partial, target, error);
    return error ? InstallAccess : 0;
  }
  std::vector<std::string> names;
  for(auto& file : install.files) if(!sameName(file.name, "PARAM.SFO")) names.push_back(file.name);
  names.push_back("PARAM.SFO");
  for(auto& name : names) {
    std::string into = savePath(install.folder, name);
    if(into.empty()) return InstallAccess;
    dropCuts(into);  //(written over: a descriptor that emptied it is cut no more, io.cpp)
    fs::rename(fs::path(install.partial) / name, into, error);
    if(error) return InstallAccess;
  }
  return 0;
}

//The dialog finished with its answer: the copy's folder taken away (made the save's folder, emptied into it, or given
//up), the progress 100 where it installed, and a front end's progress screen taken down.
auto Kernel::installFinish(u32 answer) -> void {
  u32 p = dialog.parameters;
  if(answer == 0) {
    memory.write(4, p + 1372, 100);
    installTrace(traceName(install.folder) + " -> 0x00000000 (installed), " + std::to_string(install.files.size())
               + " files " + std::to_string(install.copied) + " bytes copied");
  } else if(!install.partial.empty()) {
    installTrace(traceName(install.folder) + " -> 0x" + hexWord(answer) + " (" + installAnswerName(answer) + "), "
               + std::to_string(install.copied) + " of " + std::to_string(install.total) + " bytes copied");
  }
  installAbandon();
  memory.write(4, p + 28, answer);
  dialog.status = DialogFinished;
  dialogWithdraw();
}

//The copy's own folder taken away, with whatever's left in it (nothing, once it's become the save's), and the stick's
//folder for copies with it if that leaves it empty; nothing else. As the dialog finishes, as the game aborts it, as
//the machine is reset, and as a state is loaded. A link a host put in the place of either folder is taken away
//itself: nothing is taken away through it.
auto Kernel::installAbandon() -> void {
  namespace fs = std::filesystem;
  std::error_code error;
  if(!install.partial.empty()) {
    fs::path folder = install.partial;
    if(!fs::is_symlink(folder.parent_path(), error)) fs::remove_all(folder, error);
    fs::remove(folder.parent_path(), error);
  }
  install = {};
}

//With a front end that shows the dialogs (dialogs.cpp), the PSP's progress screen in mode 1 (in mode 0 a PSP shows
//nothing, the game drawing its own): a request for the front end to show and not answer, made afresh as the progress
//moves on. Its mode 1 and its focus the progress; its one save the install, by the data's name, with its titles from
//the parameters, the bytes it copies, whether the folder's there already (written over), and INSDIR's ICON0.PNG.
auto Kernel::installShow() -> void {
  namespace fs = std::filesystem;
  u32 p = dialog.parameters;
  if(!frontDialogs || s32(memory.read(4, p + 48)) != InstallShowsProgress) return;
  u32 progress = install.total ? u32(install.copied * 100 / install.total) : 100;
  if(progress == install.shown) return;
  install.shown = progress;
  DialogRequest request;
  request.kind = DialogInstall;
  request.language = memory.read(4, p + 4);
  request.circle = memory.read(4, p + 8) == 0;
  request.mode = InstallShowsProgress;
  request.focus = progress;
  DialogRequest::Save save;
  save.name = memory.readString(p + 68, 20);
  save.folder = install.folder;
  save.title = memory.readString(p + 88, 128);
  save.saveTitle = memory.readString(p + 216, 128);
  save.detail = memory.readString(p + 344, 1024);
  std::error_code error;
  std::string target = savePath(install.folder);
  save.there = !target.empty() && fs::is_directory(target, error);
  save.size = install.total;
  save.icon = install.icon;
  request.saves.push_back(std::move(save));
  dialogPost(std::move(request));
}

auto Kernel::sceUtilityGamedataInstallInitStart() -> void { dialogStart(DialogInstall); }
auto Kernel::sceUtilityGamedataInstallGetStatus() -> void { dialogStatus(DialogInstall); }
auto Kernel::sceUtilityGamedataInstallUpdate() -> void { dialogUpdate(DialogInstall); }
auto Kernel::sceUtilityGamedataInstallShutdownStart() -> void { dialogShutdown(DialogInstall); }

//The game stopping an install while it runs (Kingdom Hearts' code does on flags of its own): the copy given up, the
//memory stick as it was, and the dialog finished, cancelled. Not running (starting, or finished: installed already),
//the wrong status.
auto Kernel::sceUtilityGamedataInstallAbort() -> void {
  if(dialog.kind != DialogInstall) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogRunning) return result(UtilityInvalidStatus);
  if(!install.started) installTrace("aborted before it began -> 0x00000001 (cancelled)");
  installFinish(DialogCancelled);
  result(0);
}
