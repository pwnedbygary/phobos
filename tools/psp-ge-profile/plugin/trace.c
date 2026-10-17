//psp-ge-profile's trace: a line per step (the module's start; each firmware function found or not, with its address;
//each hook; the control thread; the device; the settings; the disc id; the first flip; each trigger; each file
//written or not), so a run that writes nothing else still says how far it got. Each line starts with the seconds
//since the module started. Lines are kept in memory and appended to <device>:/PHOBOS/trace.txt (never truncated) by
//module_start at the game's boot, then by the control thread whenever it may open a file, never while a capture or
//a dry run's window is on; lines from before the PHOBOS folder is found wait until it is. Past the buffer's 6 KB,
//lines are counted, not kept. The file stops at 256 KB: the lines that reach that mark are written with a note that
//it's full, and later ones are let go (deleting trace.txt starts it again).

#include "plugin.h"
#include <pspiofilemgr.h>

enum { TraceFileLimit = 256 * 1024 };
static char lines[6144];
static int used, lost;
static u32 origin;

void traceStart(u32 time) {
  origin = time;
}

void traceLine(const char* format, ...) {
  char line[192];
  Text text = {line, sizeof(line), 0};
  line[0] = 0;
  u32 milliseconds = (sceKernelGetSystemTimeLow() - origin) / 1000;
  textFormat(&text, "%4lu.%03lu ", milliseconds / 1000, milliseconds % 1000);
  va_list args;
  va_start(args, format);
  textFormatList(&text, format, args);
  va_end(args);
  while(text.length && (line[text.length - 1] == '\n' || line[text.length - 1] == '\r')) line[--text.length] = 0;
  if(used + text.length + 1 > (int)sizeof(lines)) {
    lost++;
    return;
  }
  memcpy(lines + used, line, text.length);
  used += text.length;
  lines[used++] = '\n';
}

//Returns whether the lines were written (and so let go).
int traceWrite(const char* device) {
  if(!device || (!used && !lost)) return 0;
  char path[48];
  Text text = {path, sizeof(path), 0};
  textFormat(&text, "%s/PHOBOS/trace.txt", device);
  SceIoStat status;
  memset(&status, 0, sizeof(status));
  if(sceIoGetstat(path, &status) >= 0 && status.st_size >= TraceFileLimit) {
    used = lost = 0;  //full: let go
    return 1;
  }
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if(file < 0) return 0;
  int ok = sceIoWrite(file, lines, used) == used;
  if(ok && lost) {
    char note[64];
    Text notice = {note, sizeof(note), 0};
    textFormat(&notice, "(%d lines lost: the trace's buffer was full)\n", lost);
    ok = sceIoWrite(file, note, notice.length) == notice.length;
  }
  if(ok && status.st_size + used >= TraceFileLimit) {
    static const char full[] = "(trace.txt is full at 256 KB: later lines aren't kept; delete it to start again)\n";
    ok = sceIoWrite(file, full, sizeof(full) - 1) == (int)sizeof(full) - 1;
  }
  sceIoClose(file);
  if(ok) used = lost = 0;
  return ok;
}
