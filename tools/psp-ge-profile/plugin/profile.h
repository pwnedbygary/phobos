//psp-ge-profile: the capture file's layout. The plugin writes it (main.c, hooks.c); decoder/geprof.py reads it and
//must agree with every offset here. Little-endian, as the PSP writes it. ../README.md, "The capture file", says the
//same in words.
//
//A file is a 256-byte header, then the records (recordCount of recordSize bytes each), then the thread table
//(threadCount entries of 36 bytes, at threadTableOffset): each record's thread byte is an index into that table.

#pragma once
#include <psptypes.h>

#define ProfileVersion "0.2.3"
enum { ProfileFormat = 1, ProfileHeaderSize = 256, ProfileRecordSize = 32, ProfileThreadNameSize = 32 };

//What a record is. The calls are the game's own, seen through the patched syscalls, with entry and return times;
//the interrupt kinds (the GE's SIGNAL and FINISH callbacks, vertical blanks) are seen through the interrupt
//manager's sub-interrupt monitor, just before and just after the game's handler runs.
enum {
  KindGeEnQueue = 1,          //a: list, b: stall, c: callback id; result: the list's id (or an error, < 0)
  KindGeEnQueueHead,          //as KindGeEnQueue
  KindGeDeQueue,              //a: list id
  KindGeUpdateStallAddr,      //a: list id, b: stall
  KindGeListSync,             //a: list id, b: mode (0 waits for the list's end, 1 asks); result: the list's state
  KindGeDrawSync,             //a: mode (0 waits for the queue to empty, 1 asks); result: the state
  KindGeBreak,                //a: mode, b: parameter's address
  KindGeContinue,
  KindGeSetCallback,          //a: SIGNAL handler, b: FINISH handler, c: the callback data's address; result: callback id
  KindGeUnsetCallback,        //a: callback id
  KindGeSignal,               //interrupt; a: callback id, b: the SIGNAL's low 16 bits, c: the list address after the
                              //SIGNAL/END pair; result: the SIGNAL command (0 if its address couldn't be read)
  KindGeFinish,               //interrupt; a: callback id, b: the FINISH's low 16 bits, c: the list address after FINISH/END
  KindSetFrameBuf,            //a: top address, b: buffer width, c: pixel format | sync mode << 8
  KindWaitVblankStart,
  KindWaitVblankStartCB,
  KindWaitVblank,
  KindWaitVblankCB,
  KindWaitVblankStartMulti,   //a: vblanks
  KindWaitVblankStartMultiCB, //a: vblanks
  KindVblank,                 //interrupt; a: the display's vblank count, b: the sub-interrupt it was seen at
  KindCount
};

//The GE's state when a record was made, in its ge word: the firmware's own sceGeDrawSync(1) answer, and when a list
//is drawing or stalled, the address of the GE's next command (word-aligned) in the bits above.
enum { GeIdle = 0, GeNotSampled = 1, GeDrawing = 2, GeStalled = 3 };

typedef struct {
  u8 kind;
  u8 thread;    //index into the thread table; 0: interrupt
  u16 vcount;   //the display's vblank count at entry, low 16 bits
  u32 entry;    //sceKernelGetSystemTimeLow at entry (microseconds)
  u32 exit;     //at return; for interrupts, when the game's handler returned; 0: unfinished when the capture ended
  u32 ge;       //(next command & ~3) | GeIdle..GeStalled, at entry
  u32 a, b, c;
  u32 result;
} ProfileRecord;

//Flags in the header.
enum {
  ProfileFull = 1,          //the buffer filled: dropped records weren't kept, and the capture ended there
  ProfileMonitor = 2,       //the sub-interrupt monitor was on (SIGNAL, FINISH and vblank records are there)
  ProfileTimed = 4,         //started by the settings' start time, not by the buttons
  ProfileGeSampled = 8,     //records' ge words were sampled
};

typedef struct {
  char magic[8];            //"PHGEPROF"
  u32 format;               //ProfileFormat
  u32 headerSize;           //ProfileHeaderSize
  u32 recordSize;           //ProfileRecordSize
  u32 recordCount;
  u32 flags;
  u32 dropped;              //records that didn't fit
  char pluginVersion[16];
  char discId[16];
  char title[64];           //PARAM.SFO's TITLE (UTF-8), cut to fit
  u32 startTime, endTime;   //sceKernelGetSystemTimeLow at the capture's start and end
  u32 startVcount, endVcount;
  u32 cpuMhz, busMhz;       //at the start
  u32 cpuMhzEnd, busMhzEnd;
  u32 firmware;             //sceKernelDevkitVersion
  u32 arkVersion, arkMinor; //sctrlHENGetVersion, sctrlHENGetMinorVersion (0: not found)
  u32 model;                //sceKernelGetModel (0 = PSP-1000; ~0: not found)
  u32 hooks;                //bit n: kind n's hook is in place; bit 31: the monitor
  u32 captureNumber;        //the file's n
  u32 pluginStartTime;      //sceKernelGetSystemTimeLow when the plugin started (the game's boot)
  u32 capacity;             //records the buffer held
  u32 threadTableOffset;
  u32 threadCount;
  u32 partition;            //memory partition the buffer came from
  u32 bufferBytes;
  u32 reserved[12];
} ProfileHeader;

_Static_assert(sizeof(ProfileHeader) == ProfileHeaderSize, "header size");
_Static_assert(sizeof(ProfileRecord) == ProfileRecordSize, "record size");

typedef struct {
  u32 uid;
  char name[ProfileThreadNameSize];
} ProfileThread;
