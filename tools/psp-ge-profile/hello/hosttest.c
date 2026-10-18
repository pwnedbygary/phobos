//The load test on the Mac: hello.c with the firmware faked, the memory stick being folders in a temporary one. Built
//twice by `make hosttest`, as the kernel module and as the user one (USER_MODULE). Checked: the line, appended (never
//truncating) to ms0:/PHOBOS/hello.txt, or to ef0:/PHOBOS/hello.txt when ms0: has no PHOBOS folder, nothing when
//neither has; for the kernel one, the API type, boot file and title ID, and pointers that don't point into RAM (an
//unlinked function's error code) or strings that aren't text never read or copied as they are.

#include "hello.c"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];

static void hostPath(char* out, const char* path) {
  const char* colon = strchr(path, ':');
  assert(colon);
  snprintf(out, 512, "%s/%.*s%s", root, (int)(colon - path), path, colon + 1);
}

unsigned int sceKernelGetSystemTimeLow(void) { return 12345678; }

SceUID sceIoOpen(const char* file, int flags, SceMode mode) {
  assert(flags == (PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND));
  assert(!strcmp(file, "ms0:/PHOBOS/hello.txt") || !strcmp(file, "ef0:/PHOBOS/hello.txt"));
  char path[512];
  hostPath(path, file);
  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, mode);
  return fd >= 0 ? fd : -1;
}
int sceIoWrite(SceUID fd, const void* data, SceSize size) { return write(fd, data, size); }
int sceIoClose(SceUID fd) { return close(fd); }

//Kernel RAM, as far as Readable() goes: this array.
static char ram[1024] __attribute__((aligned(16)));
int hostReadable(const void* pointer) {
  return (const char*)pointer >= ram && (const char*)pointer < ram + sizeof(ram);
}

#ifndef USER_MODULE
static int apitype = 0x123;
static char* bootFile = ram;
static SceGameInfo* gameInfo = (SceGameInfo*)(ram + 256);
static SceSize maxFree = 812032, totalFree = 1046528;
int sceKernelInitApitype(void) { return apitype; }
char* sceKernelInitFileName(void) { return bootFile; }
SceGameInfo* sceKernelGetGameInfo() { return gameInfo; }
SceSize sceKernelPartitionMaxFreeMemSize(int partition) {
  assert(partition == 1);
  return maxFree;
}
SceSize sceKernelPartitionTotalFreeMemSize(int partition) {
  assert(partition == 1);
  return totalFree;
}
#endif

static int readFile(const char* file, char* out, int size) {
  char path[512];
  hostPath(path, file);
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  int n = read(fd, out, size - 1);
  close(fd);
  out[n > 0 ? n : 0] = 0;
  return n;
}

static void folder(const char* file) {
  char path[512];
  hostPath(path, file);
  assert(mkdir(path, 0777) == 0);
}

int main(void) {
  snprintf(root, sizeof(root), "%s", "/tmp/psp-ge-profile-hello-XXXXXX");
  assert(mkdtemp(root));
  folder("ms0:/");
  folder("ef0:/");
  char text[2048];

  //no PHOBOS folder anywhere: nothing written, and still a clean start
  assert(module_start(0, 0) == 0);
  assert(readFile("ms0:/PHOBOS/hello.txt", text, sizeof(text)) < 0 && readFile("ef0:/PHOBOS/hello.txt", text, 2) < 0);

#ifdef USER_MODULE
  const char* expected = "phoboshellou loaded: user module, time 12345678 us\n";
#else
  strcpy(bootFile, "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
  strcpy(gameInfo->title_id, "UCUS98653");
  const char* expected = "phoboshello loaded: kernel module, time 12345678 us, api type 0x123, boot file "
                         "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN, title id UCUS98653, kernel memory free 812032 bytes in "
                         "one block, 1046528 bytes in all\n";
#endif

  //ef0: only (a PSP Go without the folder on its memory stick)
  folder("ef0:/PHOBOS");
  assert(module_start(0, 0) == 0);
  assert(readFile("ef0:/PHOBOS/hello.txt", text, sizeof(text)) > 0 && !strcmp(text, expected));

  //ms0: has the folder: the line goes there, appended at each start
  folder("ms0:/PHOBOS");
  assert(module_start(0, 0) == 0);
  assert(module_start(0, 0) == 0);
  assert(readFile("ms0:/PHOBOS/hello.txt", text, sizeof(text)) > 0);
  assert(!strncmp(text, expected, strlen(expected)) && !strcmp(text + strlen(expected), expected));
  assert(readFile("ef0:/PHOBOS/hello.txt", text, sizeof(text)) > 0 && !strcmp(text, expected));

#ifndef USER_MODULE
  //functions this firmware wouldn't have (their stubs give 0x8002013A), an empty title ID, a boot file that isn't
  //text and is longer than 64 characters, a game information pointer out of line
  char path[512];
  hostPath(path, "ms0:/PHOBOS/hello.txt");
  unlink(path);
  apitype = (int)0x8002013A;
  bootFile = (char*)(uintptr_t)0x8002013A;
  gameInfo = (SceGameInfo*)(uintptr_t)0x8002013A;
  maxFree = totalFree = 0x8002013A;
  assert(module_start(0, 0) == 0);
  assert(readFile("ms0:/PHOBOS/hello.txt", text, sizeof(text)) > 0);
  assert(!strcmp(text, "phoboshello loaded: kernel module, time 12345678 us, api type 0x8002013A, boot file "
                       "(unavailable: 0x8002013A), title id (unavailable: 0x8002013A), kernel memory free "
                       "0x8002013A in one block, 0x8002013A in all\n"));
  unlink(path);
  apitype = 0x141;
  bootFile = ram;
  memset(ram, 'A', 200);
  ram[3] = '\t', ram[200] = 0;
  gameInfo = (SceGameInfo*)(ram + 257);
  maxFree = 0xFFFFFFF, totalFree = 4294967295u >> 1;
  assert(module_start(0, 0) == 0);
  assert(readFile("ms0:/PHOBOS/hello.txt", text, sizeof(text)) > 0);
  char long64[65];
  memset(long64, 'A', 64);
  long64[3] = '?', long64[64] = 0;
  char want[512];
  snprintf(want, sizeof(want), "phoboshello loaded: kernel module, time 12345678 us, api type 0x141, boot file %s, "
           "title id (unavailable: 0x%X), kernel memory free 268435455 bytes in one block, 2147483647 bytes in all\n",
           long64, (unsigned)(uintptr_t)(ram + 257));
  assert(!strcmp(text, want));
  unlink(path);
  gameInfo = (SceGameInfo*)(ram + 512);
  memset(gameInfo, 0, sizeof(*gameInfo));
  assert(module_start(0, 0) == 0);
  assert(readFile("ms0:/PHOBOS/hello.txt", text, sizeof(text)) > 0 && strstr(text, ", title id (empty), kernel"));
#endif

  printf("hosttest (%s): all checks passed\n", Kind);
  return 0;
}
