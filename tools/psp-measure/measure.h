//What psp-measure's parts share: main.c (the menu), results.c (the results folder, and each test's file in it),
//vfpu.c (the VFPU's and the FPU's tests) and ge.c (the GE's and the controller's).
#pragma once

#include <pspkernel.h>
#include <pspdebug.h>

#define print pspDebugScreenPrintf

//make SMOKE=1 builds a quick version for trying the program in an emulator before a PSP runs it (PPSSPP's
//PPSSPPHeadless, which has no buttons to press): it runs round 3's VFPU and FPU tests (the big ones cut short by
//Shrink, a shift), the FPU probes and the GE's rounds 2 and 3 straight away, and leaves when done. Its results say
//nothing about a PSP.
#ifdef SMOKE
enum { Smoke = 1, Shrink = 8 };
#else
enum { Smoke = 0, Shrink = 0 };
#endif

//---- results.c

//results/ beside EBOOT.PBP, and the folder in it that the running part writes into (results/vfpu or results/ge).
extern char results[256], folder[272];
void findResults(const char* program);  //from the program's own path (argv[0]); makes results/ if it isn't there
void useFolder(const char* part);       //folder becomes results/<part>, made if it isn't there

int exists(const char* path);
int mark(const char* path);  //an empty file, as a marker; 0 if it can't be written
void awake(void);            //tells the PSP it's in use, so its power-save timer starts over

//One test's file: written as <name>.part and renamed to <name>.bin once complete (see results.c). line is the
//screen line saying how the test is doing.
typedef struct {
  char done[320], part[320], again[320];
  SceUID file;
  int line;
} Output;
int beginTrying(Output* out, const char* name, int retry);  //1: run it; 0: done or given up on; -1: can't write
int begin(Output* out, const char* name);                   //beginTrying, with one more try after a stop
int writeOut(Output* out, const char* name, const void* data, int bytes);
void progress(Output* out, const char* name, unsigned int done, unsigned int total);
int finish(Output* out, const char* name);
void drop(Output* out);  //closes the file unkept, as if the test hadn't started: the next start runs it afresh

//---- the parts' rounds, each returning 0 if a file couldn't be written (the VFPU's rounds stop there, the GE's go
//on with the rest)

void vfpuStart(void);      //first thing when the program starts: what round 1 and round 3 record as found then
int vfpuRound(int round);  //1-3, or 4 for the FPU probes
int geRound(int round);    //2: the GE's first tests and the controller's timing; 3: what they left open
void geEnd(void);          //lets the GE go before the program leaves
