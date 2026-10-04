//The results folder, and each test's file in it. Every part of psp-measure writes its tests the same way, so a round
//can always be stopped (or stop the PSP) and be started again without losing what it finished.
#include "measure.h"
#include <psppower.h>
#include <stdio.h>
#include <string.h>

char results[256], folder[272];

//results/ goes beside EBOOT.PBP: the program's own path is where the XMB started it from (argv[0]), such as
//ms0:/PSP/GAME/PSPMEASURE/EBOOT.PBP.
void findResults(const char* program) {
  strncpy(results, program ? program : "ms0:/PSP/GAME/PSPMEASURE/EBOOT.PBP", sizeof(results) - 16);
  char* slash = strrchr(results, '/');
  if(slash) *slash = 0;
  strcat(results, "/results");
  sceIoMkdir(results, 0777);
}

//Each part keeps its files in a folder of its own, since some names (manifest.txt) would otherwise clash. Made again
//each time, as starting afresh renames results/ away.
void useFolder(const char* part) {
  snprintf(folder, sizeof(folder), "%s/%s", results, part);
  sceIoMkdir(results, 0777);
  sceIoMkdir(folder, 0777);
}

int exists(const char* path) {
  SceIoStat status;
  return sceIoGetstat(path, &status) >= 0;
}

//Tells the PSP it's in use, so its power-save timer (Auto Sleep, Backlight Auto-Off) starts over: a long round with
//nothing pressed would otherwise be put to sleep part way.
void awake(void) { scePowerTick(PSP_POWER_TICK_ALL); }

int mark(const char* path) {
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return 0;
  sceIoClose(file);
  return 1;
}

//A test's file is written as <name>.part and renamed to .bin once complete, so a stopped test leaves no .bin behind
//and runs again next time. If its .part is there when it starts, the last run didn't finish it (the PSP stopped, or
//was stopped, during it): it runs once more, with <name>.again marking that; and if it doesn't finish then either,
//the next start gives up on it, renaming what it wrote to <name>.stopped, and the round goes on without it. A probe
//(retry 0) isn't run again: one stop and the next start gives up on it. Names are up to 26 characters (the GE's
//longest), so each test's line on the screen lines up.
int beginTrying(Output* out, const char* name, int retry) {
  char stopped[320];
  snprintf(out->done, sizeof(out->done), "%s/%s.bin", folder, name);
  snprintf(out->part, sizeof(out->part), "%s/%s.part", folder, name);
  snprintf(out->again, sizeof(out->again), "%s/%s.again", folder, name);
  snprintf(stopped, sizeof(stopped), "%s/%s.stopped", folder, name);
  awake();
  if(exists(out->done)) {
    print("%-26s already done\n", name);
    return 0;
  }
  if(exists(stopped)) {
    print("%-26s given up on (it stopped the PSP)\n", name);
    return 0;
  }
  int retrying = exists(out->part);
  if(retrying && (!retry || exists(out->again))) {
    sceIoRename(out->part, stopped);
    sceIoRemove(out->again);
    print("%-26s stopped the PSP%s: given up on\n", name, retry ? " twice" : "");
    return 0;
  }
  out->file = sceIoOpen(out->part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(out->file < 0) {
    print("%-26s can't write %s\n", name, out->part);
    return -1;
  }
  if(retrying) {  //marked only once it's really running again, so a file that can't be written costs no retry
    if(!mark(out->again)) {  //unmarked, a test that stops the PSP every time would never be given up on
      sceIoClose(out->file);
      print("%-26s can't write %s (memory stick full?)\n", name, out->again);
      return -1;
    }
    print("%-26s didn't finish last time: once more\n", name);
  }
  out->line = pspDebugScreenGetY();
  print("%-26s running", name);
  return 1;
}

int begin(Output* out, const char* name) { return beginTrying(out, name, 1); }

//With no .part (or .again) left, the next start runs the test afresh: for a test that didn't stop the PSP but can't
//or mustn't keep what it began.
void drop(Output* out) {
  sceIoClose(out->file);
  sceIoRemove(out->part);
  sceIoRemove(out->again);
}

int writeOut(Output* out, const char* name, const void* data, int bytes) {
  awake();  //every chunk a long test writes (256 KiB), so no stretch of work goes without telling the PSP
  if(sceIoWrite(out->file, data, bytes) == bytes) return 1;
  print("%-26s write failed (memory stick full?)\n", name);
  drop(out);  //a failed write isn't the PSP stopping
  return 0;
}

void progress(Output* out, const char* name, unsigned int done, unsigned int total) {
  awake();
  pspDebugScreenSetXY(0, out->line);
  print("%-26s %3u%%   ", name, (unsigned int)(100ull * done / total));
}

int finish(Output* out, const char* name) {
  sceIoClose(out->file);
  if(sceIoRename(out->part, out->done) < 0) {
    print("%-26s can't rename %s (memory stick?)\n", name, out->part);
    sceIoRemove(out->part);  //not the PSP stopping either: the next start runs the test afresh
    sceIoRemove(out->again);
    return 0;
  }
  sceIoRemove(out->again);
  pspDebugScreenSetXY(0, out->line);
  print("%-26s done   \n", name);
  return 1;
}
