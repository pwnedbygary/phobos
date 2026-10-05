//psp-flash0-dump: copies every file of the PSP's flash0 (the part of its built-in flash memory holding the firmware's
//modules, fonts and other system files; the settings are on flash1) into a folder beside this program on the memory
//stick, for Phobos's PSP core to use: games' fonts come from there (sceFont), and the firmware's modules could stand
//in for Phobos's own. The copies are the user's own, made from the user's PSP: they stay on the user's devices, and
//none of them goes into Phobos's repository. It only reads flash0.
//
//It walks flash0:/ folder by folder, making each folder again under <its folder>/flash0/ and copying each file in
//64 KiB pieces, working out each file's SHA-256 fingerprint as it goes. SHA256SUMS lists every file copied, in the
//form `shasum -a 256 -c SHA256SUMS` checks on a computer; failed.txt lists anything it couldn't read or write, with
//the PSP's error code. Last, it tries to copy flash0 whole, as the image the PSP keeps (the device lflash0:0,0), to
//flash0.img: that needs more rights than a homebrew program usually gets, so failing there is expected, and only
//noted. Running it again copies everything again, over the last copy.
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"

PSP_MODULE_INFO("FLASH0DUMP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define print pspDebugScreenPrintf

#ifdef SMOKE
//For an emulator: starts by itself, leaves at the end without waiting for a button, and copies a sample laid out like
//flash0 from the memory stick, as an emulator's flash0 may not list its folders.
enum { Smoke = 1 };
#define SOURCE "ms0:/flash0-sample"
#else
enum { Smoke = 0 };
#define SOURCE "flash0:"
#endif

static char home[256];         //this program's folder, such as ms0:/PSP/GAME/FLASH0DUMP
static char copies[300];       //where the copies go: <home>/flash0
static SceUID sums, failures;  //SHA256SUMS and failed.txt, open for the whole run
static int listsComplete = 1;   //every line of SHA256SUMS and failed.txt written (not so if the memory stick fills)
static unsigned int filesCopied, filesFailed;
static unsigned long long bytesCopied;
static unsigned char buffer[64 * 1024] __attribute__((aligned(64)));

//Tells the PSP it's in use, so its power-save timer (Auto Sleep, Backlight Auto-Off) starts over during a long copy.
static void awake(void) { scePowerTick(PSP_POWER_TICK_ALL); }

//A line into SHA256SUMS or failed.txt; whether all of it was written.
static int writeText(SceUID file, const char* text) {
  int size = strlen(text);
  if(sceIoWrite(file, text, size) == size) return 1;
  listsComplete = 0;
  return 0;
}

//A line in failed.txt: what couldn't be done, to which path, and the PSP's error code (negative, so shown in hex).
static void failed(const char* what, const char* path, int error) {
  char line[800];
  snprintf(line, sizeof(line), "%s %s: error 0x%08x\n", what, path, (unsigned int)error);
  writeText(failures, line);
  filesFailed++;
}

//Copies one file from (a path on a PSP device) to (a path on the memory stick), listed in SHA256SUMS as listed (its
//path from this program's folder). At most limit bytes are copied (0: the whole file); a file cut short there is
//noted in failed.txt too.
static void copyFile(const char* from, const char* to, const char* listed, unsigned int limit) {
  SceUID in = sceIoOpen(from, PSP_O_RDONLY, 0);
  if(in < 0) {
    failed("can't open", from, in);
    return;
  }
  SceUID out = sceIoOpen(to, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(out < 0) {
    sceIoClose(in);
    failed("can't write", to, out);
    return;
  }
  Sha256 hash;
  sha256Start(&hash);
  int read = 0, ok = 1;
  unsigned int total = 0;
  for(;;) {
    unsigned int wanted = sizeof(buffer);
    if(limit && limit - total < wanted) wanted = limit - total;
    if(!wanted || (read = sceIoRead(in, buffer, wanted)) <= 0) break;
    total += read;
    int written = sceIoWrite(out, buffer, read);
    if(written != read) {  //the memory stick full, most likely
      failed("can't write all of", to, written < 0 ? written : 0);
      ok = 0;
      break;
    }
    sha256Add(&hash, buffer, read);
    bytesCopied += read;
  }
  if(ok && read < 0) {
    failed("can't read all of", from, read);
    ok = 0;
  }
  if(ok && limit && total == limit) failed("stopped at the limit, maybe short of the end:", from, 0);
  sceIoClose(in);
  int closed = sceIoClose(out);
  if(ok && closed < 0) {
    failed("can't finish writing", to, closed);
    ok = 0;
  }
  if(!ok) return;
  char text[65], line[420];
  sha256Finish(&hash, text);
  snprintf(line, sizeof(line), "%s  %s\n", text, listed);
  if(writeText(sums, line)) filesCopied++;
}

//Copies the folder at name (a path from flash0's root, "" for the root itself) and everything in it.
static void copyFolder(const char* name) {
  char from[320], to[620];
  snprintf(from, sizeof(from), SOURCE "/%s", name);
  snprintf(to, sizeof(to), "%s%s%s", copies, *name ? "/" : "", name);  //no slash at the end: the PSP may refuse one
  sceIoMkdir(to, 0777);
  SceUID folder = sceIoDopen(from);
  if(folder < 0) {
    failed("can't list", from, folder);
    return;
  }
  print("%.60s\n", from);
  awake();
  SceIoDirent entry;
  for(;;) {
    memset(&entry, 0, sizeof(entry));  //the PSP reads a pointer in it (d_private), which has to be 0
    int result = sceIoDread(folder, &entry);
    if(result < 0) failed("can't list all of", from, result);
    if(result <= 0) break;
    if(!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")) continue;
    char path[300], source[320], target[620], listed[320];
    snprintf(path, sizeof(path), "%s%s%s", name, *name ? "/" : "", entry.d_name);
    if(FIO_S_ISDIR(entry.d_stat.st_mode)) {
      copyFolder(path);
      continue;
    }
    snprintf(source, sizeof(source), SOURCE "/%s", path);
    snprintf(target, sizeof(target), "%s/%s", copies, path);
    snprintf(listed, sizeof(listed), "flash0/%s", path);
    copyFile(source, target, listed, 0);
  }
  sceIoDclose(folder);
}

//flash0 whole, as the PSP keeps it: a FAT filesystem on the device lflash0:0,0. A homebrew program usually isn't
//allowed to open it; when it is, the image goes to flash0.img, listed in SHA256SUMS too (at most 64 MB, more than
//flash0 holds, in case the device doesn't say where it ends).
static void copyImage(void) {
  char to[300];
  snprintf(to, sizeof(to), "%s/flash0.img", home);
  print("lflash0:0,0 (the whole image)\n");
  copyFile("lflash0:0,0", to, "flash0.img", 64 * 1024 * 1024);
}

//Waits until X and O are let go (so one press isn't taken twice), then for one of buttons, and returns the buttons
//pressed.
static unsigned int waitFor(unsigned int buttons) {
  SceCtrlData pad;
  do sceCtrlReadBufferPositive(&pad, 1); while(pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE));
  do sceCtrlReadBufferPositive(&pad, 1); while(!(pad.Buttons & buttons));
  return pad.Buttons;
}

int main(int argc, char** argv) {
  pspDebugScreenInit();
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
  //this program's folder: its own path is where the XMB started it from (argv[0]), such as
  //ms0:/PSP/GAME/FLASH0DUMP/EBOOT.PBP
  strncpy(home, argc > 0 && argv[0] ? argv[0] : "ms0:/PSP/GAME/FLASH0DUMP/EBOOT.PBP", sizeof(home) - 1);
  char* slash = strrchr(home, '/');
  if(slash) *slash = 0;
  snprintf(copies, sizeof(copies), "%s/flash0", home);

  print("flash0 dump: copies the PSP's flash0 (its firmware's files)\n");
  print("to this program's folder on the memory stick:\n");
  print("%.60s\n\n", copies);
  print("The copies are yours: keep them on your own devices.\n\n");
  print("X: start    O: leave\n\n");
  if(!Smoke && waitFor(PSP_CTRL_CROSS | PSP_CTRL_CIRCLE) & PSP_CTRL_CIRCLE) {
    sceKernelExitGame();
    return 0;
  }

  char path[320];
  snprintf(path, sizeof(path), "%s/SHA256SUMS", home);
  sums = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  snprintf(path, sizeof(path), "%s/failed.txt", home);
  failures = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(sums < 0 || failures < 0) {
    print("Can't write to the memory stick (locked or full?)\n");
  } else {
    copyFolder("");
    copyImage();
    sceIoClose(sums);
    sceIoClose(failures);
    print("\nDone: %u files copied (%u KB), %u not.\n", filesCopied, (unsigned int)(bytesCopied / 1024), filesFailed);
    print("SHA256SUMS lists the copies; failed.txt what wasn't copied.\n");
    print("(The whole image usually can't be read: that's expected.)\n");
    if(!listsComplete) print("\nSHA256SUMS or failed.txt is incomplete: memory stick full?\n");
  }
  if(!Smoke) {
    print("\nPress X or O to leave.\n");
    waitFor(PSP_CTRL_CROSS | PSP_CTRL_CIRCLE);
  }
  sceKernelExitGame();
  return 0;
}
