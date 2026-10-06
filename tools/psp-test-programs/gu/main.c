//gu: a PSP program for the PSP core's tests (tests/psp/ge.cpp) that drives the GE through pspsdk's GU library, the
//way homebrew does: it clears the frame buffer, has its signal and finish callbacks called with the ids it gave, calls
//one display list from another, copies a picture into VRAM, and prints what it sees.
#include <pspkernel.h>
#include <pspgu.h>
#include <stdio.h>

PSP_MODULE_INFO("GU", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

static unsigned int __attribute__((aligned(16))) list[4096];
static unsigned int __attribute__((aligned(16))) called[1024];
static unsigned int __attribute__((aligned(16))) picture[8 * 4];
static volatile int signaled = -1, finished = -1;

static void onSignal(int id) { signaled = id; }
static void onFinish(int id) { finished = id; }

int main(void) {
  unsigned int* vram = (unsigned int*)0x44000000;
  sceGuInit();
  sceGuSetCallback(GU_CALLBACK_SIGNAL, onSignal);
  sceGuSetCallback(GU_CALLBACK_FINISH, onFinish);

  //a list for the other to call: it clears the screen's top-left corner green
  sceGuStart(GU_CALL, called);
  sceGuScissor(0, 0, 16, 16);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuClearColor(0xff00ff00);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuFinish();

  sceGuStart(GU_DIRECT, list);
  sceGuDrawBuffer(GU_PSM_8888, (void*)0, 512);
  sceGuDispBuffer(480, 272, (void*)0x88000, 512);
  sceGuOffset(2048 - 240, 2048 - 136);
  sceGuScissor(0, 0, 480, 272);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuClearColor(0xff336699);
  sceGuClear(GU_COLOR_BUFFER_BIT);
  sceGuSignal(GU_SIGNAL_WAIT, 7);
  sceGuCallList(called);
  sceGuFinishId(42);
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
  printf("signal %d, finish %d\n", signaled, finished);
  printf("cleared %08x, called %08x\n", vram[100 * 512 + 100], vram[5 * 512 + 5]);

  for(int n = 0; n < 32; n++) picture[n] = 0x01010101 * n;
  sceKernelDcacheWritebackAll();
  sceGuStart(GU_DIRECT, list);
  sceGuCopyImage(GU_PSM_8888, 0, 0, 8, 4, 8, picture, 200, 100, 512, (void*)0x04000000);
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
  printf("copied %08x %08x\n", vram[100 * 512 + 200], vram[103 * 512 + 207]);

  sceGuTerm();
  fflush(stdout);
  sceKernelExitGame();
  return 0;
}
