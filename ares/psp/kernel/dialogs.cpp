//The dialogs a front end shows itself (docs/psp-core.md, part 73). With the option "Dialogs" (frontDialogs), those a
//PSP shows the player don't answer at their first Update as utility.cpp's do: a message (its text or an error's
//number, its buttons), the keyboard (each field's description, text and limits), and the save dialogs that ask
//(loading, saving and deleting from a list, and one save's load, save and delete, with a PSP's questions and its
//notices that there's nothing to load or delete, or that the data is corrupted). From its first Update the dialog
//asks the front end (a DialogRequest) and runs until it answers (a DialogAnswer), the game calling Update every frame
//and running on meanwhile; the answer, taken at the next Update, finishes it as the player's choice does on a PSP.
//Every other dialog (the save modes with nothing to show, network settings, game sharing, the web browser, a game's
//data install), and every dialog with no such front end (the runner, the tests, the desktop and libretro hosts), is
//answered as utility.cpp answers it.
//
//The request and its answer go through a mailbox (dialogMutex's), which the front end reads and fills from a thread
//of its own (dialogShown(), dialogAnswer()). Each request has a number of its own, so an answer finds the request it
//answers and no other: a request withdrawn (the message aborted, the machine reset, a state loaded) or made afresh
//takes no answer meant for the one before. The mailbox isn't state. A state saved while a dialog asks holds it
//running, as any running dialog, and loaded, the dialog asks again at its next Update (a new number) where there's
//such a front end, and is answered at it as utility.cpp does where there isn't.

namespace {
  //message options (pspsdk's psputility_msgdialog.h and PPSSPP's notes): a text's sound, none, Yes and No, OK, no
  //cancel button, No chosen first
  constexpr u32 MessageTextSound = 0x001, MessageNoSound = 0x002, MessageYesNo = 0x010, MessageOk = 0x020;
  constexpr u32 MessageNoCancel = 0x080, MessageDefaultNo = 0x100;
  //what a message's parameters' sizes hold: the button pressed from the second (0x244) on, the buttons' own names
  //in the third (0x2c4)
  constexpr u32 MessageSizeButton = 0x244, MessageSizeNames = 0x2c4;
  //a message a PSP doesn't show: options it doesn't take, an error's number without its top bit
  //(pspautotests' utility/msgdialog/dialog)
  constexpr u32 MessageBadOption = 0x8011'0501, MessageBadError = 0x8011'0502;
  constexpr u32 KeyboardCancelled = 1;  //a keyboard field's result when the player backs out
  constexpr u32 IconMost = 1_MiB;       //(an ICON0.PNG is 144 by 80: anything this large isn't one)
  constexpr u64 IconsMost = 32_MiB;     //(and a request's icons all together, beyond which saves show none)
  constexpr u32 ListMost = 1024;        //(the most of a save dialog's list read: it ends at its first empty name)

  //UTF-16 (the keyboard's: a unit a character, as far as the PSP's characters go) to UTF-8: a unit that's half of a
  //pair, a character the PSP doesn't have, becomes U+FFFD.
  auto utf8(const std::vector<u16>& text) -> std::string {
    std::string bytes;
    for(u32 unit : text) {
      if(unit >= 0xd800 && unit < 0xe000) unit = 0xfffd;
      if(unit < 0x80) {
        bytes += char(unit);
      } else if(unit < 0x800) {
        bytes += char(0xc0 | unit >> 6), bytes += char(0x80 | (unit & 0x3f));
      } else {
        bytes += char(0xe0 | unit >> 12), bytes += char(0x80 | (unit >> 6 & 0x3f)), bytes += char(0x80 | (unit & 0x3f));
      }
    }
    return bytes;
  }

  //And back: characters past the first 65536 (which the PSP doesn't have), NULs, and bytes that aren't UTF-8 are
  //left out.
  auto utf16(const std::string& text) -> std::vector<u16> {
    std::vector<u16> units;
    for(u64 at = 0; at < text.size();) {
      u8 lead = text[at];
      u32 length = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3
                 : (lead & 0xf8) == 0xf0 ? 4 : 0;
      if(!length || at + length > text.size()) { at++; continue; }
      u32 character = length == 1 ? lead : lead & (0x7f >> length);
      bool valid = true;
      for(u32 n = 1; n < length; n++) {
        u8 next = text[at + n];
        valid = valid && (next & 0xc0) == 0x80;
        character = character << 6 | (next & 0x3f);
      }
      u32 least = length == 2 ? 0x80 : length == 3 ? 0x800 : length == 4 ? 0x1'0000 : 0;
      valid = valid && character >= least && !(character >= 0xd800 && character < 0xe000);
      if(!valid) { at++; continue; }
      if(character && character < 0x1'0000) units.push_back(character);
      at += length;
    }
    return units;
  }
}

//Whether the dialog running asks a front end: a message, the keyboard, and the save modes a PSP shows the player
//(2 to 7 and 10), whose names name saves (each plain: utility.cpp's plainName(); for a single save, the game's and
//its own; for a list, the game's; for mode 7, which lists every save, none). One whose names don't is refused at once
//as utility.cpp refuses it.
auto Kernel::dialogAsks(u32 kind) -> bool {
  if(kind == DialogMessage || kind == DialogKeyboard) return true;
  if(kind != DialogSavedata) return false;
  u32 p = dialog.parameters, mode = memory.read(4, p + 48);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  if(save == "<>") save.clear();
  if(mode == 7) return true;
  if(mode >= 4 && mode <= 6) return plainName(game, 13);
  if(mode == 2 || mode == 3 || mode == 10) return plainName(game, 13) && (save.empty() || plainName(save, 20));
  return false;
}

//(at an Update while the dialog runs, from dialogUpdate()) The front end's answer, if it has come, finishes the
//dialog (or, deleting from a list, asks again); with no request outstanding (the first Update, or the first since a
//state loaded) the request is made. A message a PSP doesn't show asks nothing: it finishes with its error as an
//aborted message finishes, its 8 Updates counted from the next one (utility/msgdialog/dialog: status 3 after the
//9th Update, its result the error, never 0 meanwhile). One that shows has its result 0 from the start (the same: 0
//as it runs).
auto Kernel::dialogAsk() -> void {
  DialogRequest asked;
  DialogAnswer given;
  bool outstanding = false, answered = false;
  {
    std::lock_guard lock{dialogMutex};
    outstanding = dialogAsked.id;
    if(outstanding && dialogGiven) asked = dialogAsked, given = *dialogGiven, answered = true;
  }
  if(answered) return dialogAnswered(asked, given);
  if(outstanding) return;
  u32 p = dialog.parameters;
  DialogRequest request;
  request.kind = dialog.kind;
  request.language = memory.read(4, p + 4);
  request.circle = memory.read(4, p + 8) == 0;
  if(dialog.kind == DialogMessage) {
    if(u32 error = dialogMessage(p, request)) {
      dialog.error = error;
      dialog.abortUpdates = MessageAbortUpdates;
      return;
    }
    memory.write(4, p + 28, 0);
    note("message dialog (shown): " + (request.text.empty() ? "error " + hexWord(request.error) : request.text));
  }
  if(dialog.kind == DialogKeyboard) dialogKeyboard(p, request);
  if(dialog.kind == DialogSavedata) dialogSaves(p, request);
  dialogPost(std::move(request));
}

//A request to the front end, in place of any outstanding (numbered afresh: never 0).
auto Kernel::dialogPost(DialogRequest request) -> void {
  std::lock_guard lock{dialogMutex};
  if(!++dialogRequests) dialogRequests++;
  request.id = dialogRequests;
  dialogAsked = std::move(request);
  dialogGiven.reset();
}

//No request outstanding: the dialog's finished, aborted or gone (the machine reset, a state loaded), and the front
//end takes it down.
auto Kernel::dialogWithdraw() -> void {
  std::lock_guard lock{dialogMutex};
  dialogAsked = {};
  dialogGiven.reset();
}

//(any thread) The request outstanding (id 0: none), and its number alone.
auto Kernel::dialogShown() -> DialogRequest {
  std::lock_guard lock{dialogMutex};
  return dialogAsked;
}

auto Kernel::dialogShownID() -> u32 {
  std::lock_guard lock{dialogMutex};
  return dialogAsked.id;
}

//(any thread) The front end's answer to the request outstanding, kept for the dialog's next Update; false (and
//nothing kept) for one to a request that isn't outstanding (withdrawn, made afresh, or answered already) or that
//isn't one of its choices: a button the message doesn't have, texts for another count of fields, a save not listed.
auto Kernel::dialogAnswer(const DialogAnswer& answer) -> bool {
  std::lock_guard lock{dialogMutex};
  auto& asked = dialogAsked;
  if(!asked.id || answer.id != asked.id || dialogGiven) return false;
  bool takes = false;
  if(asked.kind == DialogMessage) {
    takes = (answer.button == 1 && asked.buttons) || (answer.button == 2 && asked.buttons == 2)
         || (answer.button == 3 && asked.back);
  }
  if(asked.kind == DialogKeyboard) takes = answer.cancelled || answer.texts.size() == asked.fields.size();
  if(asked.kind == DialogSavedata) takes = answer.choice >= -1 && answer.choice < s32(asked.saves.size());
  if(takes) dialogGiven = answer;
  return takes;
}

//A message (pspsdk's pspUtilityMsgDialogParams: the common part, at 52 the mode, 0 an error's number or 1 a text, at
//56 the error, at 60 the text, 512 bytes of UTF-8; in the second size, 0x244, at 572 the options and at 576 the
//button pressed; in the third, 0x2c4, at 580 and 644 the OK and cancel buttons' own names, 64 bytes each) as the
//front end shows it; 0, or the error it finishes with where a PSP doesn't show it. utility/msgdialog/dialog: an
//error whose number lacks its top bit, ERRORCODEINVALID; options a PSP doesn't take, BADOPTION: in the second size a
//text's 0x200 (an error's options aren't looked at), in the third an error's or a text's 0x200, while a text's 0x20
//(OK) and an error's are taken. Which options each takes are PPSSPP's notes: in the second size a text's sound, Yes
//and No and No first; in the third those, no sound, OK and no cancel button, and No first only with Yes and No. The
//first size (0x23c) has no options. Yes and No are a text's in the second size and anything's in the third, OK the
//third's; the cancel button, Back, shows unless the third size's options take it away (PPSSPP's notes, all three).
auto Kernel::dialogMessage(u32 p, DialogRequest& request) -> u32 {
  u32 size = memory.read(4, p), mode = memory.read(4, p + 52), error = memory.read(4, p + 56);
  u32 options = size >= MessageSizeButton ? memory.read(4, p + 572) : 0;
  if(mode == 0 && !(error >> 31)) return MessageBadError;
  if(size == MessageSizeButton && mode == 1 && options & ~(MessageTextSound | MessageYesNo | MessageDefaultNo)) {
    return MessageBadOption;
  }
  if(size == MessageSizeNames) {
    u32 known = MessageTextSound | MessageNoSound | MessageYesNo | MessageOk | MessageNoCancel | MessageDefaultNo;
    if(options & ~known || (options & MessageDefaultNo && !(options & MessageYesNo))) return MessageBadOption;
  }
  if(mode == 0) request.error = error;
  else request.text = memory.readString(p + 60, 512);
  bool yesNo = options & MessageYesNo && (size == MessageSizeNames || mode == 1);
  bool ok = options & MessageOk && size == MessageSizeNames;
  request.buttons = yesNo ? 2 : ok ? 1 : 0;
  request.firstNo = yesNo && options & MessageDefaultNo;
  request.back = !(size == MessageSizeNames && options & MessageNoCancel);
  if(size == MessageSizeNames) {
    request.ok = memory.readString(p + 580, 64);
    request.cancel = memory.readString(p + 644, 64);
  }
  return 0;
}

//The keyboard's fields (SceUtilityOskData, as keyboard() reads them: at 8 the language, 16 the input types, 20 the
//lines, 28 the description, 32 the text at first, 36 the room and 48 the limit), each as the front end shows it: the
//most characters it takes is its room less the NUL, or its limit if that's less (0: none), and no more than the
//KeyboardMostText the kernel reads of a field's text.
auto Kernel::dialogKeyboard(u32 p, DialogRequest& request) -> void {
  u32 count = memory.read(4, p + 48), fields = memory.read(4, p + 52);
  for(u32 n = 0; n < std::min(count, KeyboardFields); n++) {
    u32 field = fields + n * KeyboardFieldSize;
    if(!memory.reaches(field, KeyboardFieldSize)) break;
    DialogRequest::Field shown;
    shown.language = memory.read(4, field + 8);
    shown.input = memory.read(4, field + 16);
    shown.lines = memory.read(4, field + 20);
    shown.description = utf8(keyboardText(memory, memory.read(4, field + 28)));
    shown.text = utf8(keyboardText(memory, memory.read(4, field + 32)));
    u32 room = memory.read(4, field + 36), limit = memory.read(4, field + 48);
    shown.most = room ? room - 1 : 0;
    if(limit) shown.most = std::min(shown.most, limit);
    shown.most = std::min(shown.most, KeyboardMostText);
    request.fields.push_back(shown);
  }
}

//The saves a save dialog shows, by its mode, from the memory stick (pspsdk's SceUtilitySavedataParam, as savedata()
//reads it). A list (4, 5, 6) shows saveNameList's names (20 bytes each, to the first empty one); loading and deleting
//show the saves there, saving every name, those not there as new data (PPSSPP's notes, as the PSP's list modes). With
//no list (none, or its first name empty) it shows the request's own save alone: where it's there, as PPSSPP's notes
//do; where it isn't, to save to as new data, chosen (PPSSPP's notes then list nothing). "<>" in a list stands for the
//game's saves (its folders whose names start with the game's): every one, chosen (PPSSPP's notes take one for each
//"<>", the first not yet listed, marked as unsure). Chosen beside a list's: loading or deleting from it also shows the
//request's own save where it's there and not listed, which utility.cpp's answers save to and load from (they take it
//before the list).
//Mode 7 shows every save on the memory stick, of any game (pspautotests' utility/savedata/deletedata: its list all
//delete went on after deleting the test's save, the PSP's memory stick holding others, and finished CANCEL); the
//others, the one save they name. Each save as its PARAM.SFO describes it, with the size of its files, the time of its
//PARAM.SFO (else of its folder), and its ICON0.PNG; broken, for the loads, where its PARAM.SFO is (DATA_BROKEN) or the
//data file named isn't there (FILE_NOT_FOUND), as loading it would find. A save without PARAM.SFO isn't broken, as
//savedata() has it (v2.0.0's saves load: a PSP's DATA_BROKEN, utility/savedata/loadbroken, would lose them), so a
//PSP's "corrupted" isn't shown for it. New data shows the game's icon for it: a single save's own icon0FileData
//(at 1412), a list's newData's (its pointer at 1476: an icon's file data, then its title's pointer), with newData's
//title, or a single save's own titles. The one chosen at first is by focus (at 1480, in the larger sizes; PPSSPP's
//names): 0 the request's save, 1 the first, 2 the last, 3 and 4 the latest and oldest there, 5 and 6 the first and
//last there, 7 and 8 the first and last new; the first where there's none such. Without details (the automatic
//answer, which looks only at which saves are there, broken or not, and when each was saved), no save's size or icon
//is read.
auto Kernel::dialogSaves(u32 p, DialogRequest& request, bool details) -> void {
  namespace fs = std::filesystem;
  u32 mode = memory.read(4, p + 48);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  std::string fileName = memory.readString(p + 100, 13);
  if(save == "<>") save.clear();
  bool loads = mode == 2 || mode == 4, saving = mode == 3 || mode == 5, list = mode >= 4 && mode <= 7;
  request.mode = mode;
  request.deleted = dialog.deleted;
  auto add = [&](const std::string& name, const std::string& folder) {
    for(auto& entry : request.saves) if(entry.name == name) return;
    DialogRequest::Save entry;
    entry.name = name;
    entry.folder = folder;
    request.saves.push_back(entry);
  };
  //SAVEDATA's folders, sorted (none without a memory stick)
  std::vector<std::string> stick;
  std::string saved, normalized;
  if(!resolve("ms0:/PSP/SAVEDATA", saved, normalized)) {
    std::error_code error;
    for(fs::directory_iterator at(saved, error), end; !error && at != end; at.increment(error)) {
      std::error_code unread;
      auto name = at->path().filename().string();
      if(at->is_directory(unread) && plainName(name, 64)) stick.push_back(name);
    }
    std::sort(stick.begin(), stick.end());
  }
  if(mode == 7) {
    for(auto& folder : stick) add(folder, folder);
  } else if(list) {
    u32 names = memory.read(4, p + 96), listed = 0;
    for(u32 n = 0; names && n < ListMost && memory.reaches(names + n * 20, 20); n++) {
      std::string name = memory.readString(names + n * 20, 20);
      if(name.empty()) break;
      listed++;
      if(name == "<>") {
        for(auto& folder : stick) {
          if(folder.compare(0, game.size(), game)) continue;
          std::string own = folder.substr(game.size());
          if(own.empty() || plainName(own, 20)) add(own, folder);
        }
      } else if(plainName(name, 20)) {
        add(name, game + name);
      }
    }
    bool own = !listed || (!saving && !save.empty());
    if(own && (save.empty() || plainName(save, 20))) add(save, game + save);
  } else {
    add(save, game + save);
  }
  u64 icons = 0;
  auto memoryFile = [&](u32 buffer, u32 size) -> std::vector<u8> {
    if(!details || !buffer || !size || size > IconMost || icons + size > IconsMost || !memory.reaches(buffer, size)) {
      return {};
    }
    std::vector<u8> bytes(size);
    memory.copyOut(bytes.data(), buffer, size);
    icons += size;
    return bytes;
  };
  for(u32 n = 0; n < request.saves.size(); n++) {
    auto& entry = request.saves[n];
    std::string host = savePath(entry.folder);
    std::error_code error;
    entry.there = !host.empty() && fs::is_directory(host, error);
    if(!entry.there) {
      if(mode == 3) {
        entry.title = memory.readString(p + 128, 128);
        entry.saveTitle = memory.readString(p + 256, 128);
        entry.detail = memory.readString(p + 384, 1024);
        entry.icon = memoryFile(memory.read(4, p + 1412), memory.read(4, p + 1420));
      }
      if(u32 data = memory.read(4, p + 1476); mode == 5 && data && memory.reaches(data, 20)) {
        entry.icon = memoryFile(memory.read(4, data), memory.read(4, data + 8));
        if(u32 title = memory.read(4, data + 16); title && memory.reaches(title, 1)) {
          entry.saveTitle = memory.readString(title, 128);
        }
      }
      continue;
    }
    SaveSFO sfo;
    bool broken = false;
    std::string described = savePath(entry.folder, "PARAM.SFO");
    if(sfo.read(described, broken) && !broken) {
      entry.title = sfo.text("TITLE");
      entry.saveTitle = sfo.text("SAVEDATA_TITLE");
      entry.detail = sfo.text("SAVEDATA_DETAIL");
    }
    if(loads) {
      std::string file = fileName.empty() ? std::string{} : savePath(entry.folder, fileName);
      entry.broken = broken || (!fileName.empty() && (file.empty() || !fs::is_regular_file(file, error)));
    }
    if(described.empty() || !fs::is_regular_file(described, error)) described = host;
    entry.time = hostTimes(described)[2];
    if(!details) continue;
    for(fs::directory_iterator at(host, error), end; !error && at != end; at.increment(error)) {
      std::error_code unread;
      if(!at->is_regular_file(unread)) continue;
      u64 size = at->file_size(unread);
      if(!unread) entry.size += size;
    }
    std::string icon = savePath(entry.folder, "ICON0.PNG");
    std::error_code unread;
    u64 iconSize = icon.empty() || !fs::is_regular_file(icon, unread) ? 0 : fs::file_size(icon, unread);
    if(!unread && iconSize && iconSize <= IconMost && icons + iconSize <= IconsMost) {
      entry.icon.resize(iconSize);
      std::ifstream stream(icon, std::ios::binary);
      if(!stream.read((char*)entry.icon.data(), iconSize)) entry.icon.clear();
      icons += entry.icon.size();
    }
  }
  //Loading and deleting show the saves there.
  if(mode == 4 || mode == 6 || mode == 7) {
    for(u32 n = request.saves.size(); n--;) {
      if(request.saves[n].there) continue;
      request.saves.erase(request.saves.begin() + n);
    }
  }
  auto& shown = request.saves;
  u32 count = shown.size(), chosen = 0;
  u32 focus = memory.read(4, p) > 1480 ? memory.read(4, p + 1480) : 0;  //(the smallest size, 0x5c8, ends before it)
  auto first = [&](bool there, bool last) {
    for(u32 n = 0; n < count; n++) {
      u32 at = last ? count - 1 - n : n;
      if(shown[at].there == there) return at;
    }
    return 0u;
  };
  if(focus == 0) {
    for(u32 n = 0; n < count; n++) if(shown[n].name == save) { chosen = n; break; }
  }
  if(focus == 2 && count) chosen = count - 1;
  if(focus == 3 || focus == 4) {
    for(u32 n = 0, found = 0; n < count; n++) {
      if(!shown[n].there) continue;
      bool better = focus == 3 ? shown[n].time > shown[chosen].time : shown[n].time < shown[chosen].time;
      if(!found++ || better) chosen = n;
    }
  }
  if(focus >= 5 && focus <= 8) chosen = first(focus <= 6, focus == 6 || focus == 8);
  request.focus = chosen;
}

//The front end's answer applied: the dialog finishes with what a PSP's player's choice gives, or (deleting from a
//list) asks again.
auto Kernel::dialogAnswered(const DialogRequest& asked, const DialogAnswer& given) -> void {
  u32 p = dialog.parameters;
  if(asked.kind == DialogMessage) {
    //Yes (or OK) 1, No 2, Back 3 where the dialog has Yes and No or is the third size and 0 otherwise (PPSSPP's
    //notes); the button pressed written in the sizes that have it (the first, 0x23c, ends before it)
    u32 size = memory.read(4, p);
    bool three = given.button == 3 && (size == MessageSizeNames || (size == MessageSizeButton && asked.buttons == 2));
    u32 button = given.button == 3 && !three ? 0 : given.button;
    if(size >= MessageSizeButton) memory.write(4, p + 576, button);
    memory.write(4, p + 28, 0);
  }
  if(asked.kind == DialogKeyboard) {
    //each field's text, as keyboard() writes them, CHANGED; backed out of, each CANCELLED (pspsdk's result) and its
    //output as it was, the common result 0 (chosen: no recording shows a PSP's keyboard backed out of)
    u32 count = memory.read(4, p + 48), fields = memory.read(4, p + 52);
    for(u32 n = 0; n < std::min<u64>(count, asked.fields.size()); n++) {
      u32 field = fields + n * KeyboardFieldSize;
      if(!memory.reaches(field, KeyboardFieldSize)) break;
      if(!given.cancelled) keyboardField(field, utf16(given.texts[n]));
      memory.write(4, field + 44, given.cancelled ? KeyboardCancelled : KeyboardChanged);
    }
    memory.write(4, p + 28, 0);
  }
  if(asked.kind == DialogSavedata) {
    auto answer = dialogSaved(p, asked, given);
    if(!answer) return;
    memory.write(4, p + 28, *answer);
  }
  dialog.status = DialogFinished;
  dialogWithdraw();
}

//A save dialog's answer: its result (none while a list delete asks again). As PPSSPP's notes have a PSP's dialogs
//(their screens and results): with nothing to load (no save listed, or a single load's save not there) the notice
//closed finishes LOAD_NO_DATA, and with nothing to delete DELETE_NO_DATA, whatever's answered; backed out of, CANCEL
//(1); a save chosen (and its question answered yes), what loading, saving or deleting it gives (savedata(): 0, or
//the error a broken save gives, as utility/savedata/loadbroken's list loads did), a list's choice written into the
//request's save name first (savedata(); PPSSPP's notes: loads and saves copy it back). Deleting from a list deletes
//the save and asks again, the list without it, until it's backed out of (CANCEL) or there's nothing left to delete
//(DELETE_NO_DATA, as utility/savedata/deletedata's list delete of the test's one save finished).
auto Kernel::dialogSaved(u32 p, const DialogRequest& asked, const DialogAnswer& given) -> std::optional<u32> {
  u32 mode = asked.mode;
  bool none = asked.saves.empty() || ((mode == 2 || mode == 10) && !asked.saves[0].there);
  if(none && (mode == 2 || mode == 4)) return SavedataLoadNoData;
  if(none && (mode == 6 || mode == 7 || mode == 10)) return SavedataDeleteNoData;
  if(given.choice < 0) return DialogCancelled;
  auto& chosen = asked.saves[given.choice];
  if(mode == 6 || mode == 7) {
    std::string host = savePath(chosen.folder);
    std::error_code error;
    if(!host.empty()) std::filesystem::remove_all(host, error);
    dialog.deleted = true;
    DialogRequest request;
    request.kind = asked.kind;
    request.language = asked.language;
    request.circle = asked.circle;
    dialogSaves(p, request);
    dialogPost(std::move(request));
    return {};
  }
  if(mode != 4 && mode != 5) return savedata(p);
  return savedata(p, &chosen.name);
}

//A save dialog no front end shows, answered as a PSP's player who confirms what it shows first. A list's load naming
//no save of its own takes the save the dialog focuses among those it shows (only saves that are there), or, that one
//broken, the first that isn't (chosen: a player told it's corrupted takes another; the focused one if all are); a
//list's save naming none takes the list's first name, whatever the focus (chosen: where the automatic answer's later
//loads will find it; a dialog focusing a new slot, 7 or 8, would spread the saves over the list, and a load focusing
//the first would then take the oldest). Either name is written into the request's save name, as a choice is
//(savedata()). Every other mode, a list request that names its save (chosen: that save), a list with no first name,
//a game's name that isn't a plain one, a list save to "<>" (the game's folder alone) and a list the dialog would show
//nothing from are answered by savedata() as before (no data to load, or a name refused). Taking the list's first name
//whether there or not, a load from a list whose first save wasn't made found no data with others there (7th Dragon
//2020 lists DATA00 to DATA49); with no name written back, Brandish: The Dark Revenant made nothing of a load from a
//list. Only which saves are there, broken or not, and their dates are looked at, not their sizes or icons: this runs
//on the emulation thread, and Brandish lists 99 names.
auto Kernel::savedataUnshown(u32 p) -> u32 {
  u32 mode = memory.read(4, p + 48), names = memory.read(4, p + 96);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  std::string first = names && memory.reaches(names, 20) ? memory.readString(names, 20) : std::string{};
  bool unnamed = save.empty() || save == "<>";
  if((mode != 4 && mode != 5) || first.empty() || !unnamed || !plainName(game, 13)) return savedata(p);
  if(mode == 5) return first != "<>" && plainName(first, 20) ? savedata(p, &first) : savedata(p);
  DialogRequest request;
  request.kind = DialogSavedata;
  dialogSaves(p, request, false);
  if(request.saves.empty()) return savedata(p);
  DialogAnswer given;
  given.choice = s32(request.focus);
  for(u32 n = 0; request.saves[request.focus].broken && n < request.saves.size(); n++) {
    if(!request.saves[n].broken) { given.choice = s32(n); break; }
  }
  return dialogSaved(p, request, given).value_or(DialogCancelled);
}

//The byte form of a request, for a front end in another language (Phobos's Android app, through ares's
//PlayStationPortable::dialogBytes()): little-endian words, a text as its length and its bytes (UTF-8), the same for
//an icon. Version 1, id, kind, language, flags (1: circle confirms); the message's text, error, buttons, flags (1 No
//first, 2 Back), OK's name, cancel's name; the keyboard's field count, each field's description, text, most, input,
//lines and language; the save dialog's mode, focus, flags (1: a save was just deleted) and save count, each save's
//name, title, save title, detail, flags (1 there, 2 broken), size and time (64 bits each), and icon. Every part is
//there whatever the kind.
auto Kernel::dialogBytes(const DialogRequest& request) -> std::vector<u8> {
  std::vector<u8> bytes;
  auto word = [&](u32 value) { for(u32 n : range(4)) bytes.push_back(value >> n * 8); };
  auto wide = [&](u64 value) { word(value); word(value >> 32); };
  auto text = [&](const auto& data) { word(data.size()); bytes.insert(bytes.end(), data.begin(), data.end()); };
  word(1), word(request.id), word(request.kind), word(request.language), word(request.circle);
  text(request.text), word(request.error), word(request.buttons), word(request.firstNo | request.back << 1);
  text(request.ok), text(request.cancel);
  word(request.fields.size());
  for(auto& field : request.fields) {
    text(field.description), text(field.text);
    word(field.most), word(field.input), word(field.lines), word(field.language);
  }
  word(request.mode), word(request.focus), word(request.deleted), word(request.saves.size());
  for(auto& save : request.saves) {
    text(save.name), text(save.title), text(save.saveTitle), text(save.detail);
    word(save.there | save.broken << 1), wide(save.size), wide(save.time), text(save.icon);
  }
  return bytes;
}

//An answer in byte form (the same words): version 1, id, the button, the choice (signed), flags (1: backed out of),
//the texts' count, each text. False if it isn't one: it ends early, has more after it, or is another version.
auto Kernel::dialogAnswerFrom(const std::vector<u8>& bytes, DialogAnswer& answer) -> bool {
  u64 at = 0;
  bool whole = true;
  auto word = [&]() -> u32 {
    if(at + 4 > bytes.size()) return whole = false, 0;
    u32 value = bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | u32(bytes[at + 3]) << 24;
    at += 4;
    return value;
  };
  answer = {};
  if(word() != 1) return false;
  answer.id = word();
  answer.button = word();
  answer.choice = s32(word());
  answer.cancelled = word() & 1;
  u32 count = word();
  for(u32 n = 0; whole && n < count; n++) {
    u32 size = word();
    if(!whole || size > bytes.size() - at) return false;
    answer.texts.emplace_back(bytes.begin() + at, bytes.begin() + at + size);
    at += size;
  }
  return whole && at == bytes.size();
}
