//The dialogs a front end shows itself (ares/psp/kernel/dialogs.cpp): a message, the keyboard and the save dialogs
//that ask the player wait from their first Update for the front end's answer, the dialog running meanwhile, and the
//answer finishes them with what a PSP's player's choice gives; answers that aren't the request's are refused; an
//abort, a reset or a loaded state takes the request down, the dialog asking again at its next Update; the other
//dialogs are answered at once, as without a front end; and the byte form a front end in another language reads and
//answers in. Called directly, the clock moved on by hand.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 Parameters = 0x0892'0000, Buffer = 0x0893'0000, Names = 0x0894'0000, Fields = 0x0895'0000;
constexpr u32 Texts = 0x0896'0000, NewData = 0x0897'0000, Icon = 0x0897'1000;
constexpr u64 Millisecond = Kernel::CPUFrequency / 1000;
using Request = Kernel::DialogRequest;
using Answer = Kernel::DialogAnswer;

//A machine whose kernel has a front end that shows the dialogs.
struct FrontMachine : KernelMachine {
  FrontMachine(bool front = true) { kernel.frontDialogs = front; }
  auto advance(u64 cycles) -> void { kernel.cycles += cycles; kernel.events(); }
  auto status(const char* kind) -> u32 { return call((std::string{kind} + "GetStatus").c_str(), {}); }
  auto update(const char* kind) -> u32 { return call((std::string{kind} + "Update").c_str(), {1}); }
  //started and running (after the longest start, a message's or the keyboard's 300 ms)
  auto start(const char* kind) -> bool {
    return call((std::string{kind} + "InitStart").c_str(), {Parameters}) == 0 && (advance(300 * Millisecond), true)
        && status(kind) == 2;
  }
  //shut down and gone (after the longest close, network settings' 200 ms)
  auto close(const char* kind) -> bool {
    return call((std::string{kind} + "ShutdownStart").c_str(), {}) == 0 && (advance(200 * Millisecond), true)
        && status(kind) == 0;
  }
  auto answer(u32 id, u32 button) -> bool { Answer a; a.id = id; a.button = button; return kernel.dialogAnswer(a); }
  auto choose(u32 id, s32 choice) -> bool { Answer a; a.id = id; a.choice = choice; return kernel.dialogAnswer(a); }
};

//A message's parameters: its size, mode (0 an error's number, 1 text), options, text and error; English, cross
//confirming; its result 0x1337 and its button 0xcccccccc, to see what's written (where the size has a button, and in
//the first size, past its end, which nothing should touch).
auto message(KernelMachine& m, u32 size, u32 mode, u32 options, const char* text, u32 error = 0) -> void {
  auto& memory = m.system.memory;
  memory.fill(Parameters, 0, 0x2c4);
  memory.write(4, Parameters, size);
  memory.write(4, Parameters + 4, 1);
  memory.write(4, Parameters + 8, 1);
  memory.write(4, Parameters + 28, 0x1337);
  memory.write(4, Parameters + 52, mode);
  memory.write(4, Parameters + 56, error);
  memory.copyIn(Parameters + 60, text, strlen(text) + 1);
  if(size >= 0x244) memory.write(4, Parameters + 572, options);
  memory.write(4, Parameters + 576, 0xcccc'cccc);
}

//A SceUtilitySavedataParam for (mode, game, save) with DATA.BIN in a 0x1000-byte buffer and dataSize bytes of data,
//and a list of save names (none: no list); titles for what it saves.
auto saveParameters(KernelMachine& m, u32 mode, const char* game, const char* save, u32 dataSize = 0,
                    std::vector<const char*> list = {}) -> void {
  auto& memory = m.system.memory;
  memory.fill(Parameters, 0, 1536);
  memory.write(4, Parameters, 1536);
  memory.write(4, Parameters + 4, 1);
  memory.write(4, Parameters + 8, 1);
  memory.write(4, Parameters + 48, mode);
  memory.copyIn(Parameters + 60, game, strlen(game) + 1);
  memory.copyIn(Parameters + 76, save, strlen(save) + 1);
  memory.copyIn(Parameters + 100, "DATA.BIN", 9);
  memory.write(4, Parameters + 116, Buffer);
  memory.write(4, Parameters + 120, 0x1000);
  memory.write(4, Parameters + 124, dataSize);
  if(!list.empty()) {
    memory.fill(Names, 0, 20 * (list.size() + 1));
    for(u32 n = 0; n < list.size(); n++) memory.copyIn(Names + n * 20, list[n], strlen(list[n]));
    memory.write(4, Parameters + 96, Names);
  }
}

auto saveTitles(KernelMachine& m, const char* title, const char* saveTitle, const char* detail) -> void {
  m.system.memory.copyIn(Parameters + 128, title, strlen(title) + 1);
  m.system.memory.copyIn(Parameters + 256, saveTitle, strlen(saveTitle) + 1);
  m.system.memory.copyIn(Parameters + 384, detail, strlen(detail) + 1);
}

//A save made as the game would (MAKEDATASECURE, answered at once with or without a front end): its data, titles.
auto makeSave(FrontMachine& m, const char* game, const char* save, const std::string& data, const char* saveTitle)
-> bool {
  saveParameters(m, 13, game, save, data.size());
  saveTitles(m, "Kenka Bancho", saveTitle, "A detail");
  m.system.memory.copyIn(Buffer, data.data(), data.size());
  if(!m.start("sceUtilitySavedata") || m.update("sceUtilitySavedata") || m.status("sceUtilitySavedata") != 3) {
    return false;
  }
  bool made = m.system.memory.read(4, Parameters + 28) == 0;
  return m.close("sceUtilitySavedata") && made;
}

//A save dialog started, running, and asking: its request (id 0 if it didn't ask).
auto asked(FrontMachine& m) -> Request {
  if(!m.start("sceUtilitySavedata") || m.update("sceUtilitySavedata") || m.status("sceUtilitySavedata") != 2) return {};
  return m.kernel.dialogShown();
}

//The request's answer taken at the next Update, which finishes the dialog: its result (0xdead'dead if it didn't).
auto finished(FrontMachine& m, const Answer& answer) -> u32 {
  if(!m.kernel.dialogAnswer(answer)) return 0xdead'dead;
  if(m.update("sceUtilitySavedata") || m.status("sceUtilitySavedata") != 3 || m.kernel.dialogShownID()) {
    return 0xdead'dead;
  }
  u32 result = m.system.memory.read(4, Parameters + 28);
  return m.close("sceUtilitySavedata") ? result : 0xdead'dead;
}

auto choice(const Request& request, s32 choice) -> Answer {
  Answer answer;
  answer.id = request.id;
  answer.choice = choice;
  return answer;
}

//A byte file beside the Android app's tests, which read the same bytes (android/app/src/test/resources).
auto golden(const char* name) -> std::filesystem::path {
  return std::filesystem::path(__FILE__).parent_path() / "../../android/app/src/test/resources" / name;
}
}

//A message: asked at its first Update (its result 0 from then, its button as the game left it), the request saying
//what the game gave (text, buttons, No first, Back, the language and which button confirms), asked once however
//many Updates come; answers to other requests, with buttons it hasn't, or a second one refused; the answer finishes
//it at the next Update, its button written by the size's rules (pspautotests' utility/msgdialog/dialog's sizes:
//Back 3 with Yes and No or in the third size, else 0; nothing past the first size's end), the request withdrawn.
static auto frontMessage() -> void {
  FrontMachine m;
  auto& memory = m.system.memory;
  struct Case { u32 size, options, button; u32 buttons; bool firstNo, back; u32 pressed; };
  std::vector<Case> cases = {
    {0x244, 0x111, 1, 2, true, true, 1},       //Yes and No, No first: Yes
    {0x244, 0x011, 2, 2, false, true, 2},      //No
    {0x244, 0x011, 3, 2, false, true, 3},      //Back, with Yes and No
    {0x244, 0x001, 3, 0, false, true, 0},      //Back alone: 0 in the second size
    {0x23c, 0x000, 3, 0, false, true, 0xcccc'cccc},  //the first size: no button written
    {0x2c4, 0x021, 1, 1, false, true, 1},      //OK
    {0x2c4, 0x021, 3, 1, false, true, 3},      //Back in the third size
    {0x2c4, 0x011, 2, 2, false, true, 2},
  };
  for(auto& test : cases) {
    message(m, test.size, 1, test.options, "Do you want to overwrite this data?");
    if(test.size == 0x2c4) memory.copyIn(Parameters + 580, "Hai", 4), memory.copyIn(Parameters + 644, "Iya", 4);
    CHECK(m.start("sceUtilityMsgDialog"), true);
    CHECK(m.kernel.dialogShownID(), 0);  //(no Update yet)
    CHECK(m.update("sceUtilityMsgDialog"), 0);
    auto request = m.kernel.dialogShown();
    CHECK(request.id != 0 && request.kind == 2 && request.language == 1 && !request.circle, true);
    CHECK(request.text == "Do you want to overwrite this data?" && request.error == 0, true);
    CHECK(request.buttons, test.buttons);
    CHECK(request.firstNo == test.firstNo && request.back == test.back, true);
    CHECK(request.ok == (test.size == 0x2c4 ? "Hai" : "") && request.cancel == (test.size == 0x2c4 ? "Iya" : ""), true);
    CHECK(memory.read(4, Parameters + 28), 0);
    CHECK(memory.read(4, Parameters + 576), 0xcccc'cccc);
    for(u32 update : range(5)) {
      (void)update;
      CHECK(m.update("sceUtilityMsgDialog"), 0);
      CHECK(m.status("sceUtilityMsgDialog"), 2);
    }
    CHECK(m.kernel.dialogShownID(), request.id);  //(asked once)
    CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0x8011'0001);  //(running)
    CHECK(m.answer(request.id + 1, test.button), false);
    CHECK(m.answer(request.id, 4), false);
    if(test.buttons != 2) CHECK(m.answer(request.id, 2), false);
    if(!test.buttons) CHECK(m.answer(request.id, 1), false);
    CHECK(m.answer(request.id, test.button), true);
    CHECK(m.answer(request.id, test.button), false);  //(answered)
    CHECK(m.status("sceUtilityMsgDialog"), 2);  //(until the next Update)
    CHECK(m.update("sceUtilityMsgDialog"), 0);
    CHECK(m.status("sceUtilityMsgDialog"), 3);
    CHECK(memory.read(4, Parameters + 576), test.pressed);
    CHECK(memory.read(4, Parameters + 28), 0);
    CHECK(m.kernel.dialogShownID(), 0);
    CHECK(m.close("sceUtilityMsgDialog"), true);
  }
  CHECK(m.notes.size(), cases.size());
  CHECK(!m.notes.empty() && m.notes[0] == "message dialog (shown): Do you want to overwrite this data?", true);
  //an error shown by its number (the second size doesn't look at an error's options); circle confirming
  message(m, 0x244, 0, 0x201, "", 0x8002'0323);
  memory.write(4, Parameters + 8, 0);
  CHECK(m.start("sceUtilityMsgDialog") && !m.update("sceUtilityMsgDialog"), true);
  auto request = m.kernel.dialogShown();
  CHECK(request.text.empty() && request.error == 0x8002'0323 && request.buttons == 0 && request.back, true);
  CHECK(request.circle, true);
  CHECK(m.answer(request.id, 3) && !m.update("sceUtilityMsgDialog") && m.status("sceUtilityMsgDialog") == 3, true);
  CHECK(memory.read(4, Parameters + 576), 0);
  CHECK(m.close("sceUtilityMsgDialog"), true);
}

//A message with no button (the third size, no cancel button and neither OK nor Yes and No) takes no answer: the game
//takes it down (Abort), which withdraws the request at once and finishes it as an aborted message, result 0 and the
//button untouched (utility/dialog/abort's count: 8 Updates from its first, which asked).
static auto frontMessageAborted() -> void {
  FrontMachine m;
  auto& memory = m.system.memory;
  message(m, 0x2c4, 1, 0x081, "Saving. Do not remove the Memory Stick.");
  CHECK(m.start("sceUtilityMsgDialog") && !m.update("sceUtilityMsgDialog"), true);
  auto request = m.kernel.dialogShown();
  CHECK(request.buttons == 0 && !request.back, true);
  for(u32 button : {1u, 2u, 3u}) CHECK(m.answer(request.id, button), false);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0);
  CHECK(m.kernel.dialogShownID(), 0);
  CHECK(m.answer(request.id, 1), false);
  for(u32 update : range(6)) {
    (void)update;
    CHECK(m.update("sceUtilityMsgDialog"), 0);
    CHECK(m.status("sceUtilityMsgDialog"), 2);
  }
  CHECK(m.update("sceUtilityMsgDialog"), 0);
  CHECK(m.status("sceUtilityMsgDialog"), 3);
  CHECK(memory.read(4, Parameters + 28), 0);
  CHECK(memory.read(4, Parameters + 576), 0xcccc'cccc);
  CHECK(m.close("sceUtilityMsgDialog"), true);
}

//Messages a PSP doesn't show (utility/msgdialog/dialog): no request, and finished at their 9th Update with their
//error (BADOPTION 0x80110501: the second size's text with 0x200, the third's error or text with 0x200, and by PPSSPP's
//notes the third's No first without Yes and No; ERRORCODEINVALID 0x80110502: an error's number without its top bit),
//the result never 0 before, the button untouched; one saved meanwhile finishes so in another machine. The second
//size's error with 0x200, and the third's with OK (0x20), are shown.
static auto frontMessageRefused() -> void {
  FrontMachine m;
  auto& memory = m.system.memory;
  struct Case { u32 size, mode, options, error, result; };
  std::vector<Case> cases = {
    {0x244, 1, 0x201, 0, 0x8011'0501}, {0x2c4, 1, 0x201, 0, 0x8011'0501}, {0x2c4, 0, 0x201, 0x8002'0323, 0x8011'0501},
    {0x2c4, 1, 0x101, 0, 0x8011'0501}, {0x23c, 0, 0, 0, 0x8011'0502}, {0x244, 0, 0x201, 0x0002'0323, 0x8011'0502},
  };
  for(auto& test : cases) {
    message(m, test.size, test.mode, test.options, "Never shown", test.error);
    CHECK(m.start("sceUtilityMsgDialog"), true);
    for(u32 update : range(8)) {
      CHECK(m.update("sceUtilityMsgDialog"), 0);
      CHECK(m.status("sceUtilityMsgDialog"), 2);
      CHECK(m.kernel.dialogShownID(), 0);
      CHECK(memory.read(4, Parameters + 28), 0x1337);
      if(update == 3) {
        auto state = saveState(m);
        FrontMachine other;
        CHECK(loadState(other, state), true);
        for(u32 more : range(4)) (void)more, other.update("sceUtilityMsgDialog");
        CHECK(other.status("sceUtilityMsgDialog"), 2);
        other.update("sceUtilityMsgDialog");
        CHECK(other.status("sceUtilityMsgDialog"), 3);
        CHECK(other.system.memory.read(4, Parameters + 28), test.result);
      }
    }
    CHECK(m.update("sceUtilityMsgDialog"), 0);
    CHECK(m.status("sceUtilityMsgDialog"), 3);
    CHECK(memory.read(4, Parameters + 28), test.result);
    CHECK(memory.read(4, Parameters + 576), 0xcccc'cccc);
    CHECK(m.close("sceUtilityMsgDialog"), true);
  }
  CHECK(m.notes.size(), 0);
  for(auto [size, mode, options] : {std::tuple{0x244u, 0u, 0x201u}, std::tuple{0x2c4u, 0u, 0x021u}}) {
    message(m, size, mode, options, "", 0x8002'0323);
    CHECK(m.start("sceUtilityMsgDialog") && !m.update("sceUtilityMsgDialog"), true);
    CHECK(m.kernel.dialogShownID() != 0, true);
    CHECK(m.kernel.dialogShown().buttons, size == 0x2c4 ? 1 : 0);
    CHECK(m.answer(m.kernel.dialogShownID(), 3) && !m.update("sceUtilityMsgDialog"), true);
    CHECK(m.close("sceUtilityMsgDialog"), true);
  }
}

//The keyboard: each field as the game gave it (its description and text as UTF-8, the most it takes: its room less
//the NUL, or its limit where that's less, its input types, lines and language); answered, each field's text as
//entered (UTF-8 to UTF-16, cut to its most, characters the PSP hasn't left out), CHANGED and the result 0; backed out
//of, each CANCELLED and its output as it was. Texts for another count of fields are refused.
static auto frontKeyboard() -> void {
  FrontMachine m;
  auto& memory = m.system.memory;
  auto write16 = [&](u32 at, const std::u16string& text) {
    for(u32 n = 0; n < text.size(); n++) memory.write(2, at + n * 2, text[n]);
    memory.write(2, at + text.size() * 2, 0);
  };
  auto read16 = [&](u32 at) {
    std::u16string text;
    for(u16 c; (c = memory.read(2, at)) && text.size() < 64; at += 2) text.push_back(c);
    return text;
  };
  struct Field { std::u16string description, initial; u32 input, lines, room, limit; };
  std::vector<Field> fields = {
    {u"Your name", u"Snake", 0, 1, 0x200, 8}, {u"Caf\u00e9 PIN", u"", 1, 1, 5, 0}, {u"", u"Kaz", 0, 3, 0, 0},
  };
  auto setUp = [&] {
    memory.fill(Parameters, 0, 64);
    memory.write(4, Parameters, 64);
    memory.write(4, Parameters + 4, 1);
    memory.write(4, Parameters + 8, 1);
    memory.write(4, Parameters + 48, fields.size());
    memory.write(4, Parameters + 52, Fields);
    for(u32 n = 0; n < fields.size(); n++) {
      u32 at = Fields + n * 52, text = Texts + n * 0x200;
      memory.fill(at, 0, 52);
      write16(text, fields[n].description);
      write16(text + 0x80, fields[n].initial);
      memory.fill(text + 0x100, 0xcc, 0x100);
      memory.write(4, at + 8, 2);
      memory.write(4, at + 16, fields[n].input);
      memory.write(4, at + 20, fields[n].lines);
      memory.write(4, at + 28, text);
      memory.write(4, at + 32, text + 0x80);
      memory.write(4, at + 36, fields[n].room);
      memory.write(4, at + 40, text + 0x100);
      memory.write(4, at + 44, 0x77);
      memory.write(4, at + 48, fields[n].limit);
    }
  };
  auto output = [&](u32 n) { return read16(Texts + n * 0x200 + 0x100); };
  auto result = [&](u32 n) { return memory.read(4, Fields + n * 52 + 44); };
  setUp();
  CHECK(m.start("sceUtilityOsk") && !m.update("sceUtilityOsk"), true);
  auto request = m.kernel.dialogShown();
  CHECK(request.kind, 3);
  CHECK(request.fields.size(), 3);
  if(request.fields.size() != 3) return;
  CHECK(request.fields[0].description == "Your name" && request.fields[0].text == "Snake", true);
  CHECK(request.fields[0].most == 8 && request.fields[0].input == 0 && request.fields[0].language == 2, true);
  CHECK(request.fields[1].description == "Caf\xc3\xa9 PIN" && request.fields[1].text.empty(), true);
  CHECK(request.fields[1].most == 4 && request.fields[1].input == 1, true);
  CHECK(request.fields[2].most == 0 && request.fields[2].lines == 3 && request.fields[2].text == "Kaz", true);
  CHECK(memory.read(4, Parameters + 28), 0);  //(not written while it asks)
  Answer answer;
  answer.id = request.id;
  answer.texts = {"Big Boss!"};
  CHECK(m.kernel.dialogAnswer(answer), false);
  answer.texts = {"Big Boss!", "1\xe2\x98\x86\xf0\x9f\x8e\xae" "2345", "Ocelot"};  //(a star, a game pad, digits)
  CHECK(m.kernel.dialogAnswer(answer), true);
  CHECK(m.update("sceUtilityOsk") == 0 && m.status("sceUtilityOsk") == 3, true);
  CHECK(output(0) == u"Big Boss" && result(0) == 2, true);
  CHECK(output(1) == u"1\u260623" && result(1) == 2, true);
  CHECK(memory.read(2, Texts + 2 * 0x200 + 0x100) == 0xcccc && result(2) == 2, true);  //(no room)
  CHECK(memory.read(4, Parameters + 28), 0);
  CHECK(m.close("sceUtilityOsk"), true);
  setUp();
  CHECK(m.start("sceUtilityOsk") && !m.update("sceUtilityOsk"), true);
  answer = {};
  answer.id = m.kernel.dialogShownID();
  answer.cancelled = true;
  CHECK(m.kernel.dialogAnswer(answer), true);
  CHECK(m.update("sceUtilityOsk") == 0 && m.status("sceUtilityOsk") == 3, true);
  for(u32 n : range(3)) CHECK(memory.read(2, Texts + n * 0x200 + 0x100) == 0xcccc && result(n) == 1, true);
  CHECK(memory.read(4, Parameters + 28), 0);
  CHECK(m.close("sceUtilityOsk"), true);
  //half a pair in the description (U+FFFD in UTF-8), a room no field has (the most the kernel reads, 1024), and an
  //answer with bytes that aren't UTF-8 (left out)
  fields = {{u"\xd800" u"X", u"", 0, 1, 0xffff'ffff, 0}};
  setUp();
  CHECK(m.start("sceUtilityOsk") && !m.update("sceUtilityOsk"), true);
  request = m.kernel.dialogShown();
  CHECK(request.fields.size() == 1 && request.fields[0].description == "\xef\xbf\xbdX", true);
  CHECK(request.fields.size() == 1 && request.fields[0].most == 1024, true);
  answer = {};
  answer.id = request.id;
  answer.texts = {"a\xff\xc3" "b\xe2\x98"};
  CHECK(m.kernel.dialogAnswer(answer) && !m.update("sceUtilityOsk"), true);
  CHECK(output(0) == u"ab" && result(0) == 2, true);
  CHECK(m.close("sceUtilityOsk"), true);
}

//Loading from a list (Kenka Bancho's LOAD GAME: save "SGL", its list SGL001 to SGL007, the latest first): with no save
//of the list there, none listed, and the notice closed finishes LOAD_NO_DATA, as a PSP's "There is no save data."
//does (utility/savedata/deletedata's list delete finished with the delete group's NO_DATA once nothing was left, as
//PPSSPP's notes have it). With saves there, they're listed in the list's order, each as its PARAM.SFO and files
//describe it, the latest chosen first; one chosen is loaded (its data read, its titles in sfoParam, its name written
//into the request's), backing out is CANCEL, and a save without the data file named is broken, chosen
//FILE_NOT_FOUND (utility/savedata/loadbroken's list load). The request's own save, there and not listed (where
//answers without a front end load from), is listed after the list's.
static auto frontListLoad() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  std::vector<const char*> list = {"SGL001", "SGL002", "SGL003", "SGL004", "SGL005", "SGL006", "SGL007"};
  auto kenka = [&] {
    saveParameters(m, 4, "ULUS10442", "SGL", 0, list);
    memory.copyIn(Parameters + 100, "PLAYDATA.BIN", 13);
    memory.write(4, Parameters + 1480, 3);  //focus: the latest
  };
  kenka();
  auto request = asked(m);
  CHECK(request.id != 0 && request.kind == 1 && request.mode == 4 && request.saves.empty(), true);
  CHECK(m.choose(request.id, 0), false);  //(nothing to choose)
  CHECK(finished(m, choice(request, -1)), 0x8011'0307);
  //two saves of the list (SGL005 the newer), an icon in SGL002
  std::string data2 = "day two's data", data5 = "day five's data";
  for(auto [save, data] : {std::pair{"SGL002", data2}, std::pair{"SGL005", data5}}) {
    saveParameters(m, 13, "ULUS10442", save, data.size());
    memory.copyIn(Parameters + 100, "PLAYDATA.BIN", 13);
    saveTitles(m, "Kenka Bancho", save == std::string{"SGL002"} ? "Day 2" : "Day 5", "Hokkaido");
    memory.copyIn(Buffer, data.data(), data.size());
    CHECK(m.start("sceUtilitySavedata") && !m.update("sceUtilitySavedata"), true);
    CHECK(memory.read(4, Parameters + 28) == 0 && m.close("sceUtilitySavedata"), true);
  }
  stick.put("PSP/SAVEDATA/ULUS10442SGL002/ICON0.PNG", "\x89PNG icon");
  namespace fs = std::filesystem;
  auto saves = stick.path / "PSP/SAVEDATA";
  auto older = fs::last_write_time(saves / "ULUS10442SGL005/PARAM.SFO") - std::chrono::hours(48);
  fs::last_write_time(saves / "ULUS10442SGL002/PARAM.SFO", older);
  kenka();
  request = asked(m);
  CHECK(request.saves.size(), 2);
  if(request.saves.size() != 2) return;
  auto& two = request.saves[0];
  auto& five = request.saves[1];
  CHECK(two.name == "SGL002" && five.name == "SGL005", true);
  CHECK(two.there && five.there && !two.broken && !five.broken, true);
  CHECK(two.title == "Kenka Bancho" && two.saveTitle == "Day 2" && two.detail == "Hokkaido", true);
  CHECK(five.saveTitle == "Day 5", true);
  CHECK(two.size, fs::file_size(saves / "ULUS10442SGL002/PLAYDATA.BIN") + fs::file_size(saves /
    "ULUS10442SGL002/PARAM.SFO") + 9);
  CHECK(std::string(two.icon.begin(), two.icon.end()) == "\x89PNG icon" && five.icon.empty(), true);
  CHECK(five.time - two.time, 48 * 3600);
  CHECK(request.focus, 1);
  CHECK(m.choose(request.id, 2), false);
  CHECK(m.choose(request.id, -2), false);
  CHECK(finished(m, choice(request, 1)), 0);
  CHECK(memory.readString(Buffer, 32) == data5, true);
  CHECK(memory.read(4, Parameters + 124), data5.size());
  CHECK(memory.readString(Parameters + 76, 20) == "SGL005", true);
  CHECK(memory.readString(Parameters + 256, 128) == "Day 5", true);
  kenka();
  CHECK(finished(m, choice(asked(m), -1)), 1);
  CHECK(memory.readString(Parameters + 76, 20) == "SGL", true);  //(backed out: as the game left it)
  //focus 0 (the request's save) on SGL002, then 2 (the last)
  kenka();
  memory.copyIn(Parameters + 76, "SGL002", 7);
  memory.write(4, Parameters + 1480, 0);
  request = asked(m);
  CHECK(request.focus, 0);
  CHECK(finished(m, choice(request, -1)), 1);
  kenka();
  memory.write(4, Parameters + 1480, 2);
  CHECK(asked(m).focus, 1);
  CHECK(finished(m, choice(m.kernel.dialogShown(), -1)), 1);
  //broken: SGL005 without its data file
  fs::remove(saves / "ULUS10442SGL005/PLAYDATA.BIN");
  kenka();
  request = asked(m);
  CHECK(request.saves.size() == 2 && !request.saves[0].broken && request.saves[1].broken, true);
  CHECK(finished(m, choice(request, 1)), 0x8011'0309);
  //the request's own save, made by answers without a front end
  stick.put("PSP/SAVEDATA/ULUS10442SGL/PLAYDATA.BIN", "an older save");
  kenka();
  request = asked(m);
  CHECK(request.saves.size() == 3 && request.saves[2].name == "SGL" && request.saves[2].there, true);
  memory.fill(Buffer, 0, 32);
  CHECK(finished(m, choice(request, 2)), 0);
  CHECK(memory.readString(Buffer, 32) == "an older save", true);
}

//Saving to a list: every name listed, those not there as new data with newData's icon and title, the first new one
//chosen first (focus 7); one chosen is saved (its data, PARAM.SFO and icons, as a save mode writes them) and its
//name written into the request's; backing out is CANCEL, nothing written.
static auto frontListSave() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  CHECK(makeSave(m, "ULUS99999", "SLOT1", "slot one", "Slot one"), true);
  auto setUp = [&] {
    saveParameters(m, 5, "ULUS99999", "", 10, {"SLOT0", "SLOT1", "SLOT2"});
    saveTitles(m, "A game", "Chapter 3", "Saved at the gate");
    memory.copyIn(Buffer, "new slot!!", 10);
    memory.write(4, Parameters + 1476, NewData);
    memory.fill(NewData, 0, 20);
    memory.copyIn(Icon, "\x89PNG new", 8);
    memory.write(4, NewData, Icon);
    memory.write(4, NewData + 4, 8);
    memory.write(4, NewData + 8, 8);
    memory.copyIn(NewData + 0x100, "New Save", 9);
    memory.write(4, NewData + 16, NewData + 0x100);
    memory.write(4, Parameters + 1480, 7);
  };
  setUp();
  auto request = asked(m);
  CHECK(request.mode == 5 && request.saves.size() == 3, true);
  if(request.saves.size() != 3) return;
  CHECK(!request.saves[0].there && request.saves[1].there && !request.saves[2].there, true);
  CHECK(request.saves[0].saveTitle == "New Save" && std::string(request.saves[0].icon.begin(),
    request.saves[0].icon.end()) == "\x89PNG new", true);
  CHECK(request.saves[1].saveTitle == "Slot one", true);
  CHECK(request.focus, 0);
  CHECK(finished(m, choice(request, -1)), 1);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
  setUp();
  CHECK(finished(m, choice(asked(m), 2)), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT2/DATA.BIN") == "new slot!!", true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT2/PARAM.SFO"), true);
  CHECK(memory.readString(Parameters + 76, 20) == "SLOT2", true);
  setUp();
  memory.write(4, Parameters + 1480, 8);  //the last new one: SLOT0, SLOT2 there now
  CHECK(asked(m).focus, 0);
  CHECK(finished(m, choice(m.kernel.dialogShown(), 1)), 0);  //over SLOT1
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT1/DATA.BIN") == "new slot!!", true);
}

//A list mode without a list (none, or its first name empty) shows the request's own save alone: not there, to save to
//as new data (chosen: PPSSPP's notes list nothing then), and once saved, to load (as PPSSPP's notes do). A save
//dialog that asked keeps asking though the game rewrites its mode meanwhile (to the sizes mode, which is answered at
//once), and is then backed out of; a list delete deletes the save it showed though the game rewrites its game's name.
//In the smallest size, which ends before focus, the first save is chosen first.
static auto frontListless() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  saveParameters(m, 5, "ULUS99999", "DATA00", 4);
  memory.copyIn(Buffer, "data", 4);
  auto request = asked(m);
  CHECK(request.saves.size() == 1 && request.saves[0].name == "DATA00" && !request.saves[0].there, true);
  CHECK(finished(m, choice(request, 0)), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999DATA00/DATA.BIN") == "data", true);
  saveParameters(m, 4, "ULUS99999", "DATA00", 0, {""});
  request = asked(m);
  CHECK(request.saves.size() == 1 && request.saves[0].there, true);
  CHECK(finished(m, choice(request, 0)), 0);
  CHECK(memory.readString(Buffer, 4) == "data", true);
  saveParameters(m, 5, "ULUS99999", "", 10, {"SLOT0"});
  request = asked(m);
  CHECK(request.id != 0 && request.mode == 5, true);
  memory.write(4, Parameters + 48, 8);
  CHECK(m.update("sceUtilitySavedata") == 0 && m.status("sceUtilitySavedata") == 2, true);
  CHECK(m.kernel.dialogShownID(), request.id);
  CHECK(finished(m, choice(request, -1)), 1);
  //the smallest size (0x5c8) ends before focus: the first chosen whatever follows it
  saveParameters(m, 5, "ULUS99999", "", 10, {"SLOT0", "SLOT1", "SLOT2"});
  memory.write(4, Parameters, 0x5c8);
  memory.write(4, Parameters + 1480, 2);
  request = asked(m);
  CHECK(request.saves.size() == 3 && request.focus == 0, true);
  CHECK(finished(m, choice(request, -1)), 1);
  //a list delete deletes the save it showed, though the game changes its name meanwhile
  CHECK(makeSave(m, "ULUS99999", "SLOT0", "ours", "Ours"), true);
  CHECK(makeSave(m, "ULES00001", "SLOT0", "theirs", "Theirs"), true);
  saveParameters(m, 6, "ULUS99999", "", 0, {"SLOT0"});
  request = asked(m);
  CHECK(request.saves.size() == 1 && request.saves[0].saveTitle == "Ours", true);
  memory.copyIn(Parameters + 60, "ULES00001", 10);
  CHECK(m.choose(request.id, 0) && !m.update("sceUtilitySavedata"), true);
  CHECK(!std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULES00001SLOT0"), true);
  CHECK(finished(m, choice(m.kernel.dialogShown(), -1)), 1);
}

//Deleting from a list: the saves there listed; one chosen is deleted and the dialog asks again (a new request, the
//list without it, the deletion said), an answer to the old request refused, until there's nothing left, whose notice
//closed finishes DELETE_NO_DATA (utility/savedata/deletedata's list delete of its one save), or it's backed out of,
//CANCEL. A state saved after a deletion asks again in another machine with the deletion said. Mode 7 lists every
//save on the memory stick, any game's (deletedata's list all delete finished CANCEL with others left).
static auto frontListDelete() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  CHECK(makeSave(m, "ULUS99999", "SLOT0", "zero", "Zero"), true);
  CHECK(makeSave(m, "ULUS99999", "SLOT1", "one", "One"), true);
  CHECK(makeSave(m, "ULES00001", "OTHER", "other", "Another game's"), true);
  saveParameters(m, 6, "ULUS99999", "", 0, {"SLOT0", "SLOT1", "SLOT2"});
  auto request = asked(m);
  CHECK(request.mode == 6 && request.saves.size() == 2 && !request.deleted, true);
  CHECK(m.choose(request.id, 0), true);
  CHECK(m.update("sceUtilitySavedata") == 0 && m.status("sceUtilitySavedata") == 2, true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
  auto again = m.kernel.dialogShown();
  CHECK(again.id != 0 && again.id != request.id && again.deleted, true);
  CHECK(again.saves.size() == 1 && again.saves[0].name == "SLOT1", true);
  CHECK(m.choose(request.id, 0), false);
  CHECK(roundTrip(m, [&](KernelMachine& n) { n.kernel.mount("ms0", stick.path.string()); }), true);
  {
    auto state = saveState(m);
    FrontMachine other;
    other.kernel.mount("ms0", stick.path.string());
    CHECK(loadState(other, state), true);
    CHECK(other.kernel.dialogShownID(), 0);
    CHECK(other.update("sceUtilitySavedata"), 0);
    auto there = other.kernel.dialogShown();
    CHECK(there.deleted && there.saves.size() == 1, true);
  }
  CHECK(m.choose(again.id, 0), true);
  CHECK(m.update("sceUtilitySavedata") == 0 && m.status("sceUtilitySavedata") == 2, true);
  auto last = m.kernel.dialogShown();
  CHECK(last.saves.empty() && last.deleted, true);
  CHECK(finished(m, choice(last, -1)), 0x8011'0347);
  CHECK(makeSave(m, "ULUS99999", "SLOT2", "two", "Two"), true);
  saveParameters(m, 6, "ULUS99999", "", 0, {"SLOT0", "SLOT1", "SLOT2"});
  request = asked(m);
  CHECK(request.saves.size() == 1 && !request.deleted, true);
  CHECK(finished(m, choice(request, -1)), 1);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT2"), true);
  saveParameters(m, 7, "ULUS99999", "", 0);
  request = asked(m);
  CHECK(request.saves.size(), 2);
  if(request.saves.size() != 2) return;
  CHECK(request.saves[0].name == "ULES00001OTHER" && request.saves[1].name == "ULUS99999SLOT2", true);
  CHECK(request.saves[0].saveTitle == "Another game's", true);
  CHECK(m.choose(request.id, 0), true);
  CHECK(m.update("sceUtilitySavedata") == 0 && m.status("sceUtilitySavedata") == 2, true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULES00001OTHER"), false);
  CHECK(finished(m, choice(m.kernel.dialogShown(), -1)), 1);
}

//One save's load, save and delete: the save the request names (there or not, as its PARAM.SFO describes it; a new
//save as the request's own titles and icon describe it); a load or delete of a save not there finishes NO_DATA in its
//group whatever's answered (a PSP's "There is no save data."), backing out is CANCEL, and saying yes loads, saves
//over or deletes it (utility/savedata/loaddata: a load of a save not there, LOAD_NO_DATA).
static auto frontSingleSaves() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  saveParameters(m, 2, "ULUS99999", "SLOT0");
  auto request = asked(m);
  CHECK(request.mode == 2 && request.saves.size() == 1 && !request.saves[0].there, true);
  CHECK(finished(m, choice(request, 0)), 0x8011'0307);
  saveParameters(m, 10, "ULUS99999", "SLOT0");
  CHECK(finished(m, choice(asked(m), -1)), 0x8011'0347);
  saveParameters(m, 3, "ULUS99999", "SLOT0", 5);
  saveTitles(m, "A game", "Chapter 1", "At the start");
  memory.copyIn(Buffer, "first", 5);
  memory.copyIn(Icon, "\x89PNG own", 8);
  memory.write(4, Parameters + 1412, Icon);
  memory.write(4, Parameters + 1416, 8);
  memory.write(4, Parameters + 1420, 8);
  request = asked(m);
  CHECK(request.saves.size() == 1 && !request.saves[0].there, true);
  if(request.saves.size() != 1) return;
  CHECK(request.saves[0].saveTitle == "Chapter 1" && request.saves[0].detail == "At the start", true);
  CHECK(std::string(request.saves[0].icon.begin(), request.saves[0].icon.end()) == "\x89PNG own", true);
  CHECK(finished(m, choice(request, 0)), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN") == "first", true);
  memory.copyIn(Buffer, "other", 5);
  request = asked(m);  //(over it now: the question a PSP asks before overwriting)
  CHECK(request.saves.size() == 1 && request.saves[0].there && request.saves[0].saveTitle == "Chapter 1", true);
  CHECK(finished(m, choice(request, -1)), 1);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN") == "first", true);
  saveParameters(m, 2, "ULUS99999", "SLOT0");
  CHECK(finished(m, choice(asked(m), -1)), 1);
  memory.fill(Buffer, 0, 16);
  CHECK(finished(m, choice(asked(m), 0)), 0);
  CHECK(memory.readString(Buffer, 16) == "first", true);
  saveParameters(m, 10, "ULUS99999", "SLOT0");
  CHECK(finished(m, choice(asked(m), -1)), 1);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), true);
  CHECK(finished(m, choice(asked(m), 0)), 0);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
}

//With a front end, the dialogs a PSP shows nothing for are answered at their first Update as without one, asking
//nothing: the save modes that don't ask (autoload, sizes, the list, files, make), network settings, game sharing,
//the web browser and a game's data install; and a save dialog whose names aren't plain is refused at once as without
//one (utility.cpp's names: a bad parameter in the load group).
static auto frontOthers() -> void {
  HostFolder stick;
  FrontMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  for(u32 mode : {0u, 8u, 11u, 12u, 14u}) {
    saveParameters(m, mode, "ULUS99999", "SLOT0");
    CHECK(m.start("sceUtilitySavedata") && !m.update("sceUtilitySavedata"), true);
    CHECK(m.status("sceUtilitySavedata"), 3);
    CHECK(m.kernel.dialogShownID(), 0);
    CHECK(m.close("sceUtilitySavedata"), true);
  }
  saveParameters(m, 4, "../GAME", "", 0, {"SLOT0"});
  CHECK(m.start("sceUtilitySavedata") && !m.update("sceUtilitySavedata"), true);
  CHECK(m.status("sceUtilitySavedata") == 3 && m.kernel.dialogShownID() == 0, true);
  CHECK(memory.read(4, Parameters + 28), 0x8011'0308);
  CHECK(m.close("sceUtilitySavedata"), true);
  struct Kind { const char* name; u32 size; };
  for(auto kind : {Kind{"sceUtilityNetconf", 0x44}, Kind{"sceUtilityGameSharing", 0x64}, Kind{"sceUtilityHtmlViewer",
                   0x70}, Kind{"sceUtilityGamedataInstall", 0x598}}) {
    memory.fill(Parameters, 0, 0x600);
    memory.write(4, Parameters, kind.size);
    CHECK(m.start(kind.name) && !m.update(kind.name), true);
    CHECK(m.status(kind.name), 3);
    CHECK(memory.read(4, Parameters + 28), 1);
    CHECK(m.kernel.dialogShownID(), 0);
    CHECK(m.close(kind.name), true);
  }
}

//A dialog asking survives a state: loaded into this machine or another, the request's withdrawn (an answer to it
//refused) and asked again at the next Update with a new number, the same; on a machine with no front end, the next
//Update answers it as utility.cpp does (Yes). A reset takes the request down too. Without a front end nothing ever
//asks.
static auto frontStates() -> void {
  FrontMachine m;
  auto& memory = m.system.memory;
  message(m, 0x244, 1, 0x011, "Continue?");
  CHECK(m.start("sceUtilityMsgDialog") && !m.update("sceUtilityMsgDialog"), true);
  auto request = m.kernel.dialogShown();
  CHECK(request.id != 0, true);
  CHECK(roundTrip(m), true);
  auto state = saveState(m);
  CHECK(loadState(m, state), true);
  CHECK(m.kernel.dialogShownID(), 0);
  CHECK(m.answer(request.id, 1), false);
  CHECK(m.status("sceUtilityMsgDialog"), 2);
  CHECK(m.update("sceUtilityMsgDialog"), 0);
  auto again = m.kernel.dialogShown();
  CHECK(again.id != 0 && again.id != request.id && again.text == "Continue?" && again.buttons == 2, true);
  CHECK(m.answer(again.id, 2) && !m.update("sceUtilityMsgDialog") && m.status("sceUtilityMsgDialog") == 3, true);
  CHECK(memory.read(4, Parameters + 576), 2);
  FrontMachine automatic(false);
  CHECK(loadState(automatic, state), true);
  CHECK(automatic.update("sceUtilityMsgDialog"), 0);
  CHECK(automatic.status("sceUtilityMsgDialog"), 3);
  CHECK(automatic.system.memory.read(4, Parameters + 576), 1);
  CHECK(automatic.kernel.dialogShownID(), 0);
  FrontMachine fresh;
  CHECK(loadState(fresh, state), true);
  CHECK(fresh.kernel.dialogShownID() == 0 && !fresh.update("sceUtilityMsgDialog"), true);
  CHECK(fresh.kernel.dialogShown().text == "Continue?", true);
  fresh.kernel.power();
  CHECK(fresh.kernel.dialogShownID(), 0);
  CHECK(fresh.kernel.frontDialogs, true);
  //nothing asks without a front end
  CHECK(m.close("sceUtilityMsgDialog"), true);
  message(automatic, 0x244, 1, 0x011, "Continue?");
  automatic.call("sceUtilityMsgDialogShutdownStart", {});
  automatic.advance(40 * Millisecond);
  CHECK(automatic.start("sceUtilityMsgDialog") && !automatic.update("sceUtilityMsgDialog"), true);
  CHECK(automatic.kernel.dialogShownID() == 0 && automatic.status("sceUtilityMsgDialog") == 3, true);
}

//The byte form, against the files the Android app's tests read: a request with every part (a message's, the
//keyboard's, a save dialog's, UTF-8 and an icon) as dialogBytes() writes it, and an answer read back; answers that
//end early, run on, are another version or have a text past their end are refused. PSP_WRITE_DIALOG_BYTES=1 writes
//the files instead.
static auto frontBytes() -> void {
  Request request;
  request.id = 7, request.kind = 1, request.language = 2, request.circle = true;
  request.text = "Caf\xc3\xa9?", request.error = 0x8002'0323, request.buttons = 2, request.firstNo = true;
  request.back = true, request.ok = "Hai", request.cancel = "Iya";
  request.fields = {{"Name", "Snake", 8, 0x0f, 1, 2}, {"PIN", "", 4, 1, 1, 0}};
  request.mode = 5, request.focus = 1, request.deleted = true;
  Request::Save first;
  first.name = "SLOT0", first.title = "A game", first.saveTitle = "Chapter 3", first.detail = "Line one\nline two";
  first.there = true, first.broken = true, first.size = 0x1'0000'0001, first.time = 1'760'000'000;
  first.icon = {0x89, 'P', 'N', 'G'};
  Request::Save second;
  second.name = "SLOT1";
  request.saves = {first, second};
  auto bytes = Kernel::dialogBytes(request);
  CHECK(bytes.size(), 262);
  std::vector<u8> answerBytes = {
    1, 0, 0, 0,  7, 0, 0, 0,  3, 0, 0, 0,  0xff, 0xff, 0xff, 0xff,  1, 0, 0, 0,  2, 0, 0, 0,
    4, 0, 0, 0, 'B', 'o', 's', 's',  5, 0, 0, 0, '1', 0xe2, 0x98, 0x86, '2',
  };
  if(std::getenv("PSP_WRITE_DIALOG_BYTES")) {
    std::ofstream(golden("psp-dialog-request.bin"), std::ios::binary).write((const char*)bytes.data(), bytes.size());
    std::ofstream(golden("psp-dialog-answer.bin"), std::ios::binary)
      .write((const char*)answerBytes.data(), answerBytes.size());
  }
  auto file = [](const char* name) {
    std::ifstream stream(golden(name), std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  };
  CHECK(file("psp-dialog-request.bin") == bytes, true);
  CHECK(file("psp-dialog-answer.bin") == answerBytes, true);
  Answer answer;
  CHECK(Kernel::dialogAnswerFrom(answerBytes, answer), true);
  CHECK(answer.id == 7 && answer.button == 3 && answer.choice == -1 && answer.cancelled, true);
  CHECK(answer.texts.size() == 2 && answer.texts[0] == "Boss" && answer.texts[1] == "1\xe2\x98\x86" "2", true);
  auto refused = [&](std::vector<u8> changed) { Answer a; return !Kernel::dialogAnswerFrom(changed, a); };
  CHECK(refused({answerBytes.begin(), answerBytes.end() - 1}), true);
  auto longer = answerBytes;
  longer.push_back(0);
  CHECK(refused(longer), true);
  auto version = answerBytes;
  version[0] = 2;
  CHECK(refused(version), true);
  auto past = answerBytes;
  past[24] = 0xff;
  CHECK(refused(past), true);
  CHECK(refused({}), true);
}

auto dialogTests() -> Tests {
  return {
    {"dialogs front end: message", frontMessage}, {"dialogs front end: message aborted", frontMessageAborted},
    {"dialogs front end: messages a PSP doesn't show", frontMessageRefused},
    {"dialogs front end: keyboard", frontKeyboard}, {"dialogs front end: list load", frontListLoad},
    {"dialogs front end: list save", frontListSave}, {"dialogs front end: list delete", frontListDelete},
    {"dialogs front end: lists without a list, parameters rewritten", frontListless},
    {"dialogs front end: one save's load, save and delete", frontSingleSaves},
    {"dialogs front end: dialogs answered at once", frontOthers}, {"dialogs front end: states", frontStates},
    {"dialogs front end: byte form", frontBytes},
  };
}

}
