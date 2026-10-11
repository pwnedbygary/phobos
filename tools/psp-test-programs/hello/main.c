//hello: the smallest real PSP program, for the PSP core's tests (tests/psp). It prints a line with pspsdk's debug
//screen and leaves, but even that uses some 45 system functions from 11 libraries (the display, threads, memory,
//files, standard output), as pspsdk's start-up code and newlib set themselves up: a fair first test of the loader
//and the HLE kernel. A pointer in data, a global variable and a format string give the loader something to move.
//The line also goes to standard output, which the tests read back as text.
#include <pspkernel.h>
#include <pspdebug.h>
#include <stdio.h>

PSP_MODULE_INFO("HELLO", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

static const char* greeting = "hello from a PSP program";
int counter = 41;

int main(void) {
  pspDebugScreenInit();
  pspDebugScreenPrintf("%s %d\n", greeting, counter + 1);
  printf("%s %d\n", greeting, counter + 1);
  fflush(stdout);
  sceKernelExitGame();
  return 0;
}
