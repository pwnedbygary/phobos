//Files and controls (ares/psp/kernel io.cpp and ctrl.cpp): the memory stick as a temporary host folder, and the
//buttons and stick as the system sets them; with PSP_TEST_PROGRAMS, a real program that uses both through newlib.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

constexpr u32 Buffer = 0x0893'0000;

static auto readBack(KernelMachine& m, u32 length) -> std::string {
  std::string text(length, '\0');
  m.system.memory.copyOut(text.data(), Buffer, length);
  return text;
}

//Opening, reading, writing, seeking and closing, with each open mode.
static auto fileBasics() -> void {
  HostFolder folder;
  folder.put("data/File.txt", "hello");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());

  u32 file = m.call("sceIoOpen", {m.string("ms0:/DATA/FILE.TXT"), 0x0001, 0});  //found whatever the case
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 5);
  CHECK(readBack(m, 5) == "hello", true);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);  //at the end
  CHECK(m.call("sceIoLseek32", {file, 0, 2}), 5);
  m.call("sceIoLseek", {file, 0, 1, 0, 0});  //a 64-bit offset: a2 low, a3 high; whence in t0
  CHECK(m.system.ipu.r[2], 1);
  CHECK(m.system.ipu.r[3], 0);
  CHECK(m.call("sceIoRead", {file, Buffer, 2}), 2);
  CHECK(readBack(m, 2) == "el", true);
  CHECK(m.call("sceIoWrite", {file, Buffer, 2}), Kernel::ErrorBadFile);  //opened for reading only
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(m.call("sceIoClose", {file}), Kernel::ErrorBadFile);

  m.system.memory.copyIn(Buffer, "ABCD", 4);
  u32 created = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0202, 0});  //write, create
  CHECK(m.call("sceIoWrite", {created, Buffer, 4}), 4);
  CHECK(m.call("sceIoRead", {created, Buffer, 4}), Kernel::ErrorBadFile);  //opened for writing only
  m.call("sceIoClose", {created});
  CHECK(folder.get("data/new.bin") == "ABCD", true);
  u32 emptied = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0402, 0});  //write, truncate
  m.call("sceIoWrite", {emptied, Buffer, 2});
  m.call("sceIoClose", {emptied});
  CHECK(folder.get("data/new.bin") == "AB", true);
  u32 appended = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0102, 0});  //write, append
  m.call("sceIoWrite", {appended, Buffer + 2, 2});
  m.call("sceIoClose", {appended});
  CHECK(folder.get("data/new.bin") == "ABCD", true);
  u32 both = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0003, 0});  //read and write, keeping it
  m.system.memory.copyIn(Buffer, "x", 1);
  m.call("sceIoLseek32", {both, 1, 0});
  m.call("sceIoWrite", {both, Buffer, 1});
  m.call("sceIoLseek32", {both, 0, 0});
  CHECK(m.call("sceIoRead", {both, Buffer, 4}), 4);
  CHECK(readBack(m, 4) == "AxCD", true);
  m.call("sceIoClose", {both});

  CHECK(m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0a02, 0}), Kernel::ErrorFileExists);  //exclusive
  CHECK(m.call("sceIoOpen", {m.string("ms0:/missing"), 0x0001, 0}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/missing"), 0x0002, 0}), Kernel::ErrorFileNotFound);  //no create
  CHECK(m.call("sceIoOpen", {m.string("ms0:/data"), 0x0001, 0}), Kernel::ErrorIsDirectory);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/data/File.txt"), 0, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), Kernel::ErrorBadFile);  //closed
}

//Folders: making, listing, status, renaming, removing, and relative paths from the working folder.
static auto fileFolders() -> void {
  HostFolder folder;
  folder.put("data/File.txt", "hello");
  folder.put("data/another.bin", "x");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());

  CHECK(m.call("sceIoMkdir", {m.string("ms0:/saves"), 0777}), 0);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/saves"), 0777}), Kernel::ErrorFileExists);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/no/such/parent"), 0777}), Kernel::ErrorFileNotFound);
  CHECK(std::filesystem::is_directory(folder.path / "saves"), true);

  CHECK(m.call("sceIoGetstat", {m.string("ms0:/saves"), Buffer}), 0);
  CHECK(m.system.memory.read(4, Buffer), 0x11ff);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/data/file.txt"), Buffer}), 0);
  CHECK(m.system.memory.read(4, Buffer), 0x21ff);
  CHECK(m.system.memory.read(4, Buffer + 8), 5);
  CHECK(m.system.memory.read(2, Buffer + 48) >= 2020, true);  //the year it was changed
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/data/none"), Buffer}), Kernel::ErrorFileNotFound);

  auto list = [&](const char* path) {
    std::vector<std::string> names;
    u32 listing = m.call("sceIoDopen", {m.string(path)});
    if(listing >= 0x8000'0000) return names;
    while(m.call("sceIoDread", {listing, Buffer}) == 1) names.push_back(m.system.memory.readString(Buffer + 88, 256));
    CHECK(m.call("sceIoDclose", {listing}), 0);
    return names;
  };
  auto data = list("ms0:/data");
  CHECK(data.size(), 4);
  if(data.size() == 4) CHECK(data[0] == "." && data[1] == ".." && data[2] == "another.bin" && data[3] == "File.txt", true);
  auto top = list("ms0:/");
  CHECK(top.size(), 2);  //no "." or ".." at a device's top
  if(top.size() == 2) CHECK(top[0] == "data" && top[1] == "saves", true);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/data/File.txt")}), Kernel::ErrorNotDirectory);

  CHECK(m.call("sceIoChdir", {m.string("ms0:/data")}), 0);
  CHECK(m.kernel.workingDirectory == "ms0:/data", true);
  u32 relative = m.call("sceIoOpen", {m.string("FILE.TXT"), 0x0001, 0});
  CHECK(m.call("sceIoRead", {relative, Buffer, 5}), 5);
  m.call("sceIoClose", {relative});
  CHECK(m.call("sceIoChdir", {m.string("File.txt")}), Kernel::ErrorNotDirectory);

  CHECK(m.call("sceIoRename", {m.string("ms0:/data/File.txt"), m.string("ms0:/data/Renamed.txt")}), 0);
  CHECK(std::filesystem::exists(folder.path / "data/Renamed.txt"), true);
  CHECK(m.call("sceIoRename", {m.string("ms0:/data/another.bin"), m.string("ms0:/data/renamed.txt")}),
        Kernel::ErrorFileExists);
  CHECK(m.call("sceIoRmdir", {m.string("ms0:/data")}), Kernel::ErrorDirectoryNotEmpty);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/renamed.TXT")}), 0);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/another.bin")}), 0);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/another.bin")}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/saves")}), Kernel::ErrorIsDirectory);
  CHECK(m.call("sceIoRmdir", {m.string("ms0:/data")}), 0);
  CHECK(std::filesystem::exists(folder.path / "data"), false);
}

//Paths stay inside their device's folder; unmounted devices, aliases and backslashes.
static auto fileContainment() -> void {
  HostFolder outer;
  std::filesystem::path stick = outer.path / "stick";
  std::filesystem::create_directories(stick / "data");
  KernelMachine m;
  m.kernel.mount("ms0", stick.string());
  for(const char* path : {"ms0:/../outside.txt", "ms0:/data/../../outside.txt", "../outside.txt"}) {
    CHECK(m.call("sceIoOpen", {m.string(path), 0x0202, 0}), Kernel::ErrorFileNotFound);
  }
  CHECK(std::filesystem::exists(outer.path / "outside.txt"), false);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/C:/windows.txt"), 0x0202, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/PARAM.SFO"), 0x0001, 0}), Kernel::ErrorDeviceNotFound);
  HostFolder disc;
  disc.put("PSP_GAME/PARAM.SFO", "sfo");
  m.kernel.mount("disc0", disc.path.string());
  u32 aliased = m.call("sceIoOpen", {m.string("umd0:/psp_game/param.sfo"), 0x0001, 0});
  CHECK(m.call("sceIoRead", {aliased, Buffer, 8}), 3);
  u32 backslashed = m.call("sceIoOpen", {m.string("disc0:\\PSP_GAME\\PARAM.SFO"), 0x0001, 0});
  CHECK(backslashed >= 3 && backslashed < 0x8000'0000, true);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/./data/./new.txt"), 0x0202, 0}) < 0x8000'0000, true);
  CHECK(std::filesystem::exists(stick / "data/new.txt"), true);

  //symbolic links in the folder: one leading out of it is refused, file or folder; one staying inside works
  outer.put("secret.txt", "secret");
  std::filesystem::create_symlink(outer.path / "secret.txt", stick / "link");
  std::filesystem::create_directory_symlink(outer.path, stick / "outward");
  std::filesystem::create_directory_symlink(stick / "data", stick / "inward");
  CHECK(m.call("sceIoOpen", {m.string("ms0:/link"), 0x0001, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/outward/secret.txt"), 0x0001, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/outward/made.txt"), 0x0202, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/link"), Buffer}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/outward")}), Kernel::ErrorNoPermission);
  CHECK(std::filesystem::exists(outer.path / "made.txt"), false);
  u32 inward = m.call("sceIoOpen", {m.string("ms0:/inward/new.txt"), 0x0001, 0});
  CHECK(inward >= 3 && inward < 0x8000'0000, true);
}

//A directory entry's private part, when the program asks for it: the short 8.3 name in capitals, and the long name.
static auto fileShortNames() -> void {
  HostFolder folder;
  folder.put("Verylongname.extension", "");
  folder.put("File.txt", "");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());
  constexpr u32 Extra = Buffer + 0x1000;
  u32 listing = m.call("sceIoDopen", {m.string("ms0:/")});
  std::vector<std::pair<std::string, std::string>> names;
  m.system.memory.write(4, Buffer + 344, Extra);
  while(m.call("sceIoDread", {listing, Buffer}) == 1) {
    names.push_back({m.system.memory.readString(Extra + 4, 13), m.system.memory.readString(Extra + 20, 1024)});
  }
  CHECK(names.size(), 2);
  if(names.size() == 2) {
    CHECK(names[0].first == "FILE.TXT" && names[0].second == "File.txt", true);
    CHECK(names[1].first == "VERYLONG.EXT" && names[1].second == "Verylongname.extension", true);
  }
}

//The buttons and stick: sampled at each vertical blank and kept; the latest peeked at once, oldest first, each with
//the time it was taken; inverted when negative; the stick only when sampled in analog mode; buttons only the system
//sees left out; the counts allowed.
static auto controllerPeek() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x4010 | 0x10'0000;  //cross and up, and volume up, which games don't see
  m.kernel.controller.analogX = 200;
  vblank();
  u32 taken = u32(m.kernel.cycles / (Kernel::CPUFrequency / 1'000'000));
  m.kernel.cycles += Kernel::CPUFrequency / 100;  //some time passes before the program looks
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 2}), 2);
  CHECK(m.system.memory.read(4, Buffer + 4), 0);  //the one before: nothing held, as if sampled since power on
  CHECK(m.system.memory.read(1, Buffer + 8), 128);
  CHECK(m.system.memory.read(4, Buffer + 16), taken);  //the latest last, with the time it was taken
  CHECK(m.system.memory.read(4, Buffer + 16 + 4), 0x4010);
  CHECK(m.system.memory.read(1, Buffer + 16 + 8), 128);  //digital: the stick reads as centred
  CHECK(m.call("sceCtrlSetSamplingMode", {1}), 0);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(1, Buffer + 8), 128);  //that sample was taken before the change
  vblank();
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(1, Buffer + 8), 200);
  CHECK(m.system.memory.read(1, Buffer + 9), 128);
  CHECK(m.call("sceCtrlPeekBufferNegative", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(4, Buffer + 4), ~0x4010u);
  m.call("sceCtrlGetSamplingMode", {Buffer});
  CHECK(m.system.memory.read(4, Buffer), 1);
  //the count is a byte, and the PSP keeps 64 samples: 64 or more is refused, and a count that wraps can't overflow
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 64}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 0x1000'0040}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 0x1000'0001}), 1);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 63}), 63);
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 64}), Kernel::ErrorInvalidSize);
}

//The latch: presses and releases between reads, the buttons held and not held at some sample meanwhile, and how many
//samples; reading starts it afresh, and doesn't wait for another sample.
static auto controllerLatch() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x4010;  //cross and up
  vblank();
  m.call("sceCtrlReadLatch", {Buffer});
  m.kernel.controller.buttons = 0x4010 | 0x2000;  //circle pressed
  vblank();
  m.kernel.controller.buttons = 0x4010;           //and let go
  vblank();
  CHECK(m.call("sceCtrlPeekLatch", {Buffer}), 2);
  CHECK(m.system.memory.read(4, Buffer + 0), 0x2000);     //made
  CHECK(m.system.memory.read(4, Buffer + 4), 0x2000);     //broken
  CHECK(m.system.memory.read(4, Buffer + 8), 0x6010);     //held at some sample
  CHECK(m.system.memory.read(4, Buffer + 12), ~0x4010u);  //not held at some sample
  //the same into a buffer at an unaligned address in VRAM's fourth copy, whose words cross its 32-byte pieces
  constexpr u32 Unaligned = 0x0460'001e;
  CHECK(m.call("sceCtrlPeekLatch", {Unaligned}), 2);
  CHECK(m.system.memory.read(4, Unaligned + 0), 0x2000);
  CHECK(m.system.memory.read(4, Unaligned + 12), ~0x4010u);
  CHECK(m.call("sceCtrlReadLatch", {Buffer}), 2);         //the same, then it starts afresh
  CHECK(m.system.memory.read(4, Buffer + 8), 0x6010);
  CHECK(m.call("sceCtrlReadLatch", {Buffer}), 0);         //at once, with nothing gathered
  CHECK(m.system.memory.read(4, Buffer + 0), 0);
  CHECK(m.system.memory.read(4, Buffer + 8), 0);
}

//Reading the buffer when samples have come since the last read: those samples at once, oldest first, and how many
//(at most 63 wait); only the newest if there are more than asked for; places past the newest filled from the oldest
//kept.
static auto controllerReadNew() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x1000;  //triangle, for longer than the 64 samples kept
  for(u32 n = 0; n < 70; n++) vblank();
  CHECK(m.kernel.controller.unread, 63);
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 1}), 1);
  for(u32 buttons : {0x10u, 0x20u, 0x40u}) m.kernel.controller.buttons = buttons, vblank();  //up, right, down
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 4}), 3);
  CHECK(m.system.memory.read(4, Buffer + 0 * 16 + 4), 0x10);
  CHECK(m.system.memory.read(4, Buffer + 1 * 16 + 4), 0x20);
  CHECK(m.system.memory.read(4, Buffer + 2 * 16 + 4), 0x40);
  CHECK(m.system.memory.read(4, Buffer + 3 * 16 + 4), 0x1000);
  CHECK(m.kernel.controller.unread, 0);
  for(u32 buttons : {0x80u, 0x100u, 0x200u}) m.kernel.controller.buttons = buttons, vblank();  //left, L, R
  CHECK(m.call("sceCtrlReadBufferNegative", {Buffer, 2}), 2);
  CHECK(m.system.memory.read(4, Buffer + 0 * 16 + 4), ~0x100u);
  CHECK(m.system.memory.read(4, Buffer + 1 * 16 + 4), ~0x200u);
}

//A sampling cycle: its range, a sample on its timer though no frame has ended, about three a frame at 5555
//microseconds (none from the vertical blank meanwhile), and back to one at each vertical blank.
static auto controllerCycle() -> void {
  KernelMachine m;
  CHECK(m.call("sceCtrlSetSamplingCycle", {100}), Kernel::ErrorInvalidValue);
  CHECK(m.call("sceCtrlSetSamplingCycle", {20001}), Kernel::ErrorInvalidValue);
  CHECK(m.call("sceCtrlSetSamplingCycle", {5555}), 0);
  m.call("sceCtrlGetSamplingCycle", {Buffer});
  CHECK(m.system.memory.read(4, Buffer), 5555);
  m.kernel.controller.buttons = 0x4000;
  m.kernel.cycles = m.kernel.controller.nextSample;
  m.kernel.events();
  CHECK(m.kernel.controller.sampled, 0x4000);
  CHECK(m.kernel.vblanks, 0);
  m.kernel.cycles = m.kernel.nextVblank;
  m.kernel.events();
  u32 samples = m.kernel.controller.latch.samples;
  m.kernel.cycles = m.kernel.nextVblank;  //one whole frame later
  m.kernel.events();
  CHECK(m.kernel.controller.latch.samples - samples, 3);
  CHECK(m.call("sceCtrlSetSamplingCycle", {0}), 5555);
  samples = m.kernel.controller.latch.samples;
  m.kernel.controller.buttons = 0;
  m.kernel.cycles = m.kernel.nextVblank;
  m.kernel.events();
  CHECK(m.kernel.controller.latch.samples - samples, 1);
  CHECK(m.kernel.controller.sampled, 0);
}

//A program waits for the cross button, reading the controller each frame: by reading alone, which waits for the next
//sample, or by waiting for the frame and then reading, which finds that frame's sample there at once. Either way it
//keeps up with the frames: the player presses the cross on the tenth, and the tenth read sees it.
static auto controllerRead() -> void {
  for(bool waitForFrame : {false, true}) for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.put(addu(s0, zero, zero));
    u32 loop = main.here();
    if(waitForFrame) main.call("sceDisplayWaitVblankStart");
    main.li(a0, Buffer); main.li(a1, 1);
    main.call("sceCtrlReadBufferPositive");
    main.put(addiu(s0, s0, 1));
    main.li(t0, Buffer); main.put(lw(t1, 4, t0));
    main.put(andi(t1, t1, 0x4000));
    main.put(beq(t1, zero, int32_t(loop - (main.here() + 4)) / 4));
    main.put(nop);
    main.li(t0, KernelMachine::Results); main.put(sw(s0, 0, t0));
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    while(!m.kernel.exited && m.kernel.vblanks < 100) {
      if(m.kernel.vblanks == 9) m.kernel.controller.buttons = 0x4000;
      m.kernel.run(Kernel::VblankCycles);
    }
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, KernelMachine::Results), 10);  //one read per frame, the tenth with the cross
  }
}

//Two threads reading at once: the PSP lets one wait for the next sample, and refuses the other.
static auto controllerTwoReaders() -> void {
  KernelMachine m;
  for(u32 n : {0u, 1u}) {
    Assembler reader{m, 0x0880'1000 + n * 0x100};
    reader.li(a0, Buffer + n * 16); reader.li(a1, 1);
    reader.call("sceCtrlReadBufferPositive");
    reader.li(t0, KernelMachine::Results + n * 4); reader.put(sw(v0, 0, t0));
    reader.put(addu(a0, zero, zero)); reader.call("sceKernelExitThread");
  }
  m.system.power(0x0880'1000);
  for(u32 n : {0u, 1u}) {  //the first has the higher priority, so it's first to read
    s32 uid = m.kernel.createThread("reader", 0x0880'1000 + n * 0x100, 0x20 + n, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  }
  m.kernel.run(Kernel::VblankCycles * 2);
  CHECK(m.system.memory.read(4, KernelMachine::Results + 0), 1);  //waited, then had the next sample
  CHECK(m.system.memory.read(4, KernelMachine::Results + 4), Kernel::ErrorEventFlagMulti);
}

//tools/psp-test-programs' system program, through newlib: it writes a file and reads it back (whole, and from the
//middle after a seek), reads one the host put there, lists the folder, then waits for the cross button and reports
//the stick.
static auto systemProgram() -> void {
  const char* programs = testPrograms();
  if(!programs) return;
  for(bool recompile : {false, true}) {
    HostFolder stick;
    stick.put("data/given.txt", "given by the host\n");
    std::ifstream stream(std::string(programs) + "/system.elf", std::ios::binary);
    std::vector<u8> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    KernelMachine m;
    m.system.recompiler.enabled = recompile;
    m.kernel.mount("ms0", stick.path.string());
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/PSP/GAME/SYSTEM/EBOOT.PBP", error), true);
    while(!m.kernel.exited && m.kernel.vblanks < 200) {
      if(m.kernel.vblanks == 20) {
        m.kernel.controller.buttons = 0x4000;
        m.kernel.controller.analogX = 200;
        m.kernel.controller.analogY = 50;
      }
      m.kernel.run(Kernel::VblankCycles);
    }
    CHECK(m.kernel.exited, true);
    std::string expected =
      "read back: written by a PSP program\n"
      "from the middle: a PSP program\n"
      "given: given by the host\n"
      "entry: .\nentry: ..\nentry: given.txt\nentry: written.txt\n";
    CHECK(m.output.substr(0, expected.size()) == expected, true);
    CHECK(m.output.find("stick 200,50\n") != std::string::npos, true);
    if(m.output.substr(0, expected.size()) != expected) std::printf("  output: [%s]\n", m.output.c_str());
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
    CHECK(stick.get("data/written.txt") == "written by a PSP program\n", true);
  }
}

//Renaming, as pspautotests' io/file/rename recorded: the file takes the new path's last name in its own folder,
//whatever folder that path names (from ms0:/PSP, "../t2.txt" renamed to "t2a.txt", or "../t3.txt" to
//"ms0:/PSP/t3a.txt", lands in ms0:/); a name taken already, the old one itself among them, FILE_EXISTS (so a new path
//into a folder that isn't there finds the old file's own name taken); another device XDEV; an old file or folder that
//isn't there FILE_NOT_FOUND; wildcards INVALID_ARGUMENT. And Peace Walker's: its working folder on the disc, a
//temporary file on the memory stick renamed to a bare "TDLSFILE.SYS" stays beside it (it was refused as read-only).
static auto fileRename() -> void {
  HostFolder stick;
  for(const char* name : {"t1.txt", "t2.txt", "t3.txt", "PSP/keep", "PSP/SAVEDATA/GAME/TEMP0000.PW0"}) {
    stick.put(name, name);
  }
  auto image = disc_image::makeIso({{"PSP_GAME/USRDIR/DATA.BIN", {1, 2, 3}}});
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discFrom(image.bytes);
  auto rename = [&](const char* from, const char* to) {
    return m.call("sceIoRename", {m.string(from), m.string(to)});
  };
  auto there = [&](const char* name) { return std::filesystem::exists(stick.path / name); };
  CHECK(rename("ms0:/t1.txt", "ms0:/t1a.txt"), 0);
  CHECK(there("t1a.txt") && !there("t1.txt"), true);
  CHECK(rename("ms0:/t1a.txt", "ms0:/t2.txt"), Kernel::ErrorFileExists);
  CHECK(m.call("sceIoChdir", {m.string("ms0:/PSP")}), 0);
  CHECK(rename("../t2.txt", "t2a.txt"), 0);
  CHECK(there("t2a.txt") && !there("PSP/t2a.txt"), true);  //its own folder, not the working one
  CHECK(rename("../t3.txt", "ms0:/PSP/t3a.txt"), 0);
  CHECK(there("t3a.txt") && !there("PSP/t3a.txt"), true);  //nor the one the new path names
  stick.put("t1.txt", "again");
  stick.put("t2.txt", "again");
  CHECK(rename("ms0:/t1.txt", "host0:/t1.txt"), Kernel::ErrorCrossDevice);
  CHECK(rename("ms0:/t2.txt", "ms0:/NOT_THERE/t2.txt"), Kernel::ErrorFileExists);
  CHECK(rename("ms0:/NOT_THERE/t3.txt", "ms0:/t3.txt"), Kernel::ErrorFileNotFound);
  CHECK(rename("ms0:/t3.txt", "ms0:/t3b.txt"), Kernel::ErrorFileNotFound);
  CHECK(rename("ms0:/t1.txt", "ms0:/t1.txt"), Kernel::ErrorFileExists);
  CHECK(rename("ms0:/t*.txt", "ms0:/t1b.txt"), Kernel::ErrorInvalidArgument);
  CHECK(rename("ms0:/t1.txt", "ms0:/t*.txt"), Kernel::ErrorInvalidArgument);
  CHECK(rename("ms0:/t?.txt", "ms0:/t?.txt"), Kernel::ErrorInvalidArgument);
  CHECK(stick.get("t1.txt") == "again" && stick.get("t2.txt") == "again", true);  //nothing refused moved
  CHECK(m.call("sceIoChdir", {m.string("disc0:/PSP_GAME/USRDIR")}), 0);
  CHECK(rename("ms0:/PSP/SAVEDATA/GAME/TEMP0000.PW0", "TDLSFILE.SYS"), 0);
  CHECK(there("PSP/SAVEDATA/GAME/TDLSFILE.SYS") && !there("PSP/SAVEDATA/GAME/TEMP0000.PW0"), true);
  CHECK(rename("disc0:/PSP_GAME/USRDIR/DATA.BIN", "OTHER.BIN"), Kernel::ErrorReadOnly);
  CHECK(roundTrip(m, [&](KernelMachine& n) {
    n.kernel.mount("ms0", stick.path.string());
    n.kernel.disc = discFrom(image.bytes);
  }), true);
}

//scePspNpDrm_user without DRM: a licensee key set and cleared, a name checked, and a game's own file on the stick
//readied and measured while open (its size, as nothing is decrypted), then read as it is; a file not open, or a
//folder, is BAD_FILE.
static auto downloadDrm() -> void {
  HostFolder folder;
  folder.put("PSP/GAME/NPUH00000/DATA.EDAT", "the game's own data");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());
  m.system.memory.fill(Buffer, 0x5a, 16);
  CHECK(m.call("sceNpDrmSetLicenseeKey", {Buffer}), 0);
  CHECK(m.call("sceNpDrmRenameCheck", {m.string("ms0:/PSP/GAME/NPUH00000/DATA.EDAT")}), 0);
  u32 file = m.call("sceIoOpen", {m.string("ms0:/PSP/GAME/NPUH00000/DATA.EDAT"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceNpDrmEdataSetupKey", {file}), 0);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), 19);
  CHECK(m.call("sceIoRead", {file, Buffer, 64}), 19);
  CHECK(readBack(m, 19) == "the game's own data", true);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), 19);  //wherever it's read to
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(m.call("sceNpDrmEdataSetupKey", {file}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {0xdead'beef}), Kernel::ErrorBadFile);
  u32 directory = m.call("sceIoDopen", {m.string("ms0:/PSP/GAME/NPUH00000")});
  CHECK(directory < 0x8000'0000, true);
  CHECK(m.call("sceNpDrmEdataSetupKey", {directory}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {directory}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmClearLicenseeKey", {}), 0);
  CHECK(m.notes.size(), 0);
}

auto fileTests() -> Tests {
  return {
    {"files basics", fileBasics}, {"files folders", fileFolders}, {"files containment", fileContainment},
    {"files rename", fileRename},
    {"files short names", fileShortNames}, {"controller peek", controllerPeek}, {"controller latch", controllerLatch},
    {"controller new samples", controllerReadNew}, {"controller cycle", controllerCycle},
    {"controller read", controllerRead}, {"controller two readers", controllerTwoReaders},
    {"system program", systemProgram}, {"download DRM on a game's own files", downloadDrm},
  };
}

}
