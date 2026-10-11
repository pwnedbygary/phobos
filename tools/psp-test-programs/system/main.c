//system: a PSP program for the PSP core's tests (tests/psp/files.cpp) that uses the memory stick and the controls the
//way programs normally do, through the C library: it writes a file and reads it back (also from the middle, which
//seeks with sceIoLseek's 64-bit offset), reads one the host put there, lists the folder, then waits for the cross
//button and reports the stick, printing each step to standard output.
#include <pspkernel.h>
#include <pspctrl.h>
#include <dirent.h>
#include <stdio.h>

PSP_MODULE_INFO("SYSTEM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

int main(void) {
  char line[64] = {0};
  FILE* file = fopen("ms0:/data/written.txt", "w");
  fputs("written by a PSP program\n", file);
  fclose(file);
  file = fopen("ms0:/data/written.txt", "r");
  fgets(line, sizeof(line), file);
  printf("read back: %s", line);
  fseek(file, 11, SEEK_SET);
  fgets(line, sizeof(line), file);
  fclose(file);
  printf("from the middle: %s", line);

  file = fopen("ms0:/data/given.txt", "r");
  if(file) {
    fgets(line, sizeof(line), file);
    fclose(file);
    printf("given: %s", line);
  }

  DIR* folder = opendir("ms0:/data");
  struct dirent* entry;
  while(folder && (entry = readdir(folder))) printf("entry: %s\n", entry->d_name);
  if(folder) closedir(folder);

  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
  SceCtrlData pad;
  int frames = 0;
  do {
    sceCtrlReadBufferPositive(&pad, 1);
    frames++;
  } while(!(pad.Buttons & PSP_CTRL_CROSS));
  printf("cross after %d frames, stick %d,%d\n", frames, pad.Lx, pad.Ly);
  fflush(stdout);
  sceKernelExitGame();
  return 0;
}
