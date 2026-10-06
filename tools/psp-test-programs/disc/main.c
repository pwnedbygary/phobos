//disc: a program run from a disc image, for the PSP core's tests (tests/psp/ares). The test puts it in an ISO as
//PSP_GAME/SYSDIR/EBOOT.BIN, beside a data file of its own making, and boots the disc. The program waits for the drive
//as games do, then reads the disc every way games read it: a file by its path, its status (where it starts on the
//disc), its first sector through an ioctl, a run of sectors by number ("sce_lbn"), umd0: itself a sector at a time,
//a folder's listing, and a path relative to its own folder. Each result goes to standard output as a line, which the
//test compares with what the disc it made holds.
#include <pspkernel.h>
#include <pspumd.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("DISC", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

//FNV-1a: a fingerprint of some bytes, to tell what was read without printing it all.
static unsigned fingerprint(const unsigned char* data, int size) {
  unsigned hash = 2166136261u;
  for(int n = 0; n < size; n++) hash = (hash ^ data[n]) * 16777619u;
  return hash;
}

static unsigned char buffer[4 * 2048];

int main(void) {
  printf("medium %d\n", sceUmdCheckMedium());
  printf("activate %d\n", sceUmdActivate(1, "disc0:"));
  printf("wait %d\n", sceUmdWaitDriveStat(PSP_UMD_READY));
  printf("drive %x\n", sceUmdGetDriveStat());

  SceUID file = sceIoOpen("disc0:/PSP_GAME/USRDIR/DATA.BIN", PSP_O_RDONLY, 0);
  int got = sceIoRead(file, buffer, sizeof(buffer));
  printf("data %d %08x\n", got, fingerprint(buffer, got));
  unsigned sector = 0;
  printf("ioctl %d\n", sceIoIoctl(file, 0x01020006, NULL, 0, &sector, sizeof(sector)));
  sceIoClose(file);

  SceIoStat status;
  memset(&status, 0, sizeof(status));
  sceIoGetstat("disc0:/PSP_GAME/USRDIR/DATA.BIN", &status);
  printf("stat %d %x %d\n", (int)status.st_size, status.st_mode, status.st_private[0] == sector);

  char path[64];
  sprintf(path, "disc0:/sce_lbn0x%x_size0x%x", sector, 4096);
  file = sceIoOpen(path, PSP_O_RDONLY, 0);
  got = sceIoRead(file, buffer, sizeof(buffer));
  sceIoClose(file);
  printf("lbn %d %08x\n", got, fingerprint(buffer, got));

  file = sceIoOpen("umd0:", PSP_O_RDONLY, 0);
  sceIoLseek32(file, sector, PSP_SEEK_SET);
  got = sceIoRead(file, buffer, 1);  //one sector
  sceIoClose(file);
  printf("umd0 %d %08x\n", got, fingerprint(buffer, 2048));

  SceUID folder = sceIoDopen("disc0:/PSP_GAME");
  SceIoDirent entry;
  memset(&entry, 0, sizeof(entry));
  while(sceIoDread(folder, &entry) > 0) {
    printf("entry %s%s\n", entry.d_name, FIO_S_ISDIR(entry.d_stat.st_mode) ? "/" : "");
    memset(&entry, 0, sizeof(entry));
  }
  sceIoDclose(folder);

  file = sceIoOpen("../USRDIR/DATA.BIN", PSP_O_RDONLY, 0);  //from PSP_GAME/SYSDIR, where it started
  printf("relative %d\n", file >= 0);
  if(file >= 0) sceIoClose(file);
  printf("write %08x\n", (unsigned)sceIoOpen("disc0:/PSP_GAME/NEW.BIN", PSP_O_WRONLY | PSP_O_CREAT, 0777));
  fflush(stdout);
  sceKernelExitGame();
  return 0;
}
