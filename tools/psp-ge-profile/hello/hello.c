//psp-ge-profile's load test: a module whose module_start appends one line to <device>:/PHOBOS/hello.txt and
//returns 0, and does nothing else (no hooks, no threads, no imports from ARK: pspsdk's libraries only). Built twice
//from this file: phoboshello.prx, a kernel module made the way ../plugin's phobosprof.prx is (the same compiler
//flags, module attributes and exports), and phoboshellou.prx (USER_MODULE), the same as a user module. Loaded by ARK
//at a game's boot next to phobosprof.prx, it tells "ARK doesn't start such a module there" (no line) from
//"phobosprof.prx itself fails" (a line, but no trace.txt). The kernel one's line also has what ARK's plugin lines
//are matched against: the boot's API type (0x120 a UMD, 0x123 to 0x126 an ISO or a PSN game, 0x141 or 0x152
//homebrew), the boot file, and the title ID in the system's game information, where ARK puts the game ID it
//compares PLUGINS.TXT's game IDs with; and the kernel memory free as the plugins load (phobosprof.prx takes some
//53 KB of it in one block). Pointers from the firmware are read only if they point into RAM, so a function this
//firmware lacks (its stub returns an error code) can't make the module read a wild address.

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdint.h>
#ifndef USER_MODULE
#include <pspinit.h>
#include <pspsysmem_kernel.h>
#endif

#ifdef HOST
#undef PSP_MODULE_INFO
#define PSP_MODULE_INFO(name, attributes, major, minor)
int hostReadable(const void* pointer);
#define Readable(pointer) hostReadable(pointer)
#else
#define Readable(pointer) \
  (((u32)(pointer) >= 0x88000000u && (u32)(pointer) < 0x8A000000u) || \
   ((u32)(pointer) >= 0x08800000u && (u32)(pointer) < 0x0A000000u))
#endif

#ifdef USER_MODULE
PSP_MODULE_INFO("PhobosHelloUser", PSP_MODULE_USER | PSP_MODULE_NO_STOP, 0, 1);
#define Module "phoboshellou"
#define Kind "user"
#else
PSP_MODULE_INFO("PhobosHello", PSP_MODULE_KERNEL | PSP_MODULE_NO_STOP, 0, 1);
#define Module "phoboshello"
#define Kind "kernel"
#endif

typedef struct {
  char text[320];
  int length;
} Line;

static void put(Line* line, const char* text) {
  while(*text && line->length < (int)sizeof(line->text) - 1) line->text[line->length++] = *text++;
}

static void putNumber(Line* line, u32 value, u32 base) {
  char digits[12];
  int n = 0;
  do {
    digits[n++] = "0123456789ABCDEF"[value % base];
    value /= base;
  } while(value);
  if(base == 16) put(line, "0x");
  char one[2] = {0, 0};
  while(n) {
    one[0] = digits[--n];
    put(line, one);
  }
}

#ifndef USER_MODULE
static void putUnavailable(Line* line, const void* pointer) {
  put(line, "(unavailable: ");
  putNumber(line, (u32)(uintptr_t)pointer, 16);
  put(line, ")");
}

//At most size characters of a string the firmware keeps, printable ones only.
static void putFirmwareText(Line* line, const char* text, int size) {
  if(!Readable(text)) {
    putUnavailable(line, text);
    return;
  }
  if(!text[0]) {
    put(line, "(empty)");
    return;
  }
  char one[2] = {0, 0};
  for(int n = 0; n < size && text[n]; n++) {
    one[0] = text[n] >= ' ' && text[n] <= '~' ? text[n] : '?';
    put(line, one);
  }
}

static void putTitleId(Line* line) {
  SceGameInfo* game = sceKernelGetGameInfo();
  if(Readable(game) && !((uintptr_t)game & 3)) putFirmwareText(line, game->title_id, sizeof(game->title_id));
  else putUnavailable(line, game);
}

//A size in bytes, or the error code a missing function gives.
static void putSize(Line* line, u32 bytes) {
  putNumber(line, bytes, bytes >= 0x80000000u ? 16 : 10);
  if(bytes < 0x80000000u) put(line, " bytes");
}
#endif

static int append(const char* path, const Line* line) {
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if(file < 0) return 0;
  int ok = sceIoWrite(file, line->text, line->length) == line->length;
  sceIoClose(file);
  return ok;
}

int module_start(SceSize args, void* argp) {
  (void)args, (void)argp;
  Line line;
  line.length = 0;
  put(&line, Module " loaded: " Kind " module, time ");
  putNumber(&line, sceKernelGetSystemTimeLow(), 10);
  put(&line, " us");
#ifndef USER_MODULE
  put(&line, ", api type ");
  putNumber(&line, (u32)sceKernelInitApitype(), 16);
  put(&line, ", boot file ");
  putFirmwareText(&line, sceKernelInitFileName(), 64);
  put(&line, ", title id ");
  putTitleId(&line);
  put(&line, ", kernel memory free ");
  putSize(&line, sceKernelPartitionMaxFreeMemSize(1));
  put(&line, " in one block, ");
  putSize(&line, sceKernelPartitionTotalFreeMemSize(1));
  put(&line, " in all");
#endif
  line.text[line.length++] = '\n';
  if(!append("ms0:/PHOBOS/hello.txt", &line)) append("ef0:/PHOBOS/hello.txt", &line);
  return 0;
}
