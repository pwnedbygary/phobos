//What psp-measure's parts share: main.c (the menu), results.c (the results folder, and each test's file in it),
//vfpu.c (the VFPU's and the FPU's tests), ge.c (the GE's and the controller's) and cpu.c (the CPU's, in round 8).
#pragma once

#include <pspkernel.h>
#include <pspdebug.h>

#define print pspDebugScreenPrintf

//make SMOKE=1 builds a quick version for trying the program in an emulator before a PSP runs it (PPSSPP's
//PPSSPPHeadless, which has no buttons to press): it runs round 3's VFPU and FPU tests (the big ones cut short by
//Shrink, a shift), the FPU probes and the GE's rounds 2 to 10b straight away, and leaves when done. Its results say
//nothing about a PSP.
#ifdef SMOKE
enum { Smoke = 1, Shrink = 8 };
#else
enum { Smoke = 0, Shrink = 0 };
#endif

//How long the GE may take over a list before it's taken to have stalled, in microseconds: every list here takes a
//small part of it. (In round 4 a PSP froze where the GE didn't finish patches cut past 64 times, when this program
//still waited for the GE as long as it took.) Every wait for the GE, ge.c's and cpu.c's, keeps to it.
enum { Patience = 5000000 };

//---- results.c

//results/ beside EBOOT.PBP, and the folder in it that the running part writes into (results/vfpu or results/ge).
extern char results[256], folder[272];
void findResults(const char* program);  //from the program's own path (argv[0]); makes results/ if it isn't there
void useFolder(const char* part);       //folder becomes results/<part>, made if it isn't there

int exists(const char* path);
int mark(const char* path);  //an empty file, as a marker; 0 if it can't be written
void awake(void);            //tells the PSP it's in use, so its power-save timer starts over

//One test's file: written as <name>.part and renamed to <name>.bin once complete (see results.c). stalled is where
//a test says the GE stalled on it (ge.c). line is the screen line saying how the test is doing.
typedef struct {
  char done[320], part[320], again[320], stalled[320];
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

void vfpuStart(void);      //as the program starts (after displayAtStart's two display calls): what round 1 and round 3
                           //record as found then
int vfpuRound(int round);  //1-3, or 4 for the FPU probes
int geRound(int round);    //2: the GE's first tests and the controller's timing; 3: what they left open; 4:
                           //parts 29's and 33's; 5: what part 48 left open; 6: what parts 60 and 64 left open;
                           //7: anti-aliased lines (part 78); 8: the GE's time again, and the CPU's (part 107); 9:
                           //triangles, texture loads, vertex formats, time-lists again (part 110); 10: CLUT32,
                           //16-bit targets, perspective, mipmaps, lights, the CPU beside the GE (part 112); 11:
                           //round 10b, the CPU polling, before and beside, and placement (part 112)
void geEnd(void);          //lets the GE go before the program leaves
//The GE taken to have stalled on the test `out` is running, `look` (the call that looked: sceGeDrawSync(1) or
//sceGeListSync(id, 1)) still giving `state` after `waited` microseconds: noted in its <name>.stalled with what its
//list held, then the GE reset (ge.c)
void geReset(const Output* out, unsigned int waited, int state, const char* look, const char* what);

//---- cpu.c: round 8's tests of the CPU alone, into results/ge as the round's other files, and what ge.c's test of the
//CPU beside the GE (cpu-contention) uses

int cpuTests(void);                                      //0 if a file couldn't be written
void cpuManifest(SceUID file);                           //their lines of manifest8.txt
void displayAtStart(void);  //what the display holds as the program starts (round 8's display-start): first of all
unsigned int cpuRead(const void* from, unsigned int bytes);  //reads every word (through from's own addresses, cached
                                                             //or not), returns their sum
void cpuChain(void* at, unsigned int nodes, unsigned int apart);  //a chain of pointers `apart` bytes apart, in a
                                                                  //scrambled order, through all of them
unsigned int cpuChase(const void* at, unsigned int steps);   //follows it that many steps
