//psp-measure: records what a real PSP computes and draws, so Phobos's PSP core can be made to match it exactly: its
//VFPU and FPU (vfpu.c; docs/psp-vfpu-measurements.md), and its GE and controller (ge.c; docs/psp-core.md, "Measuring
//the GE and the controller on a PSP"). One program, with a menu: the measurements come in rounds, each answering what
//the ones before left open.
//
//Build with pspdev's toolchain (https://github.com/pspdev/pspdev): make, which gives EBOOT.PBP (make SMOKE=1 gives
//the quick version for emulators in measure.h). Run: copy EBOOT.PBP to a folder under PSP/GAME on the memory stick
//(say PSP/GAME/PSPMEASURE) and start it from the XMB; it needs custom firmware that runs homebrew. Up and down pick a
//line of the menu, X runs it. Results go to results/ beside EBOOT.PBP: the VFPU's and the FPU's in results/vfpu, the
//GE's in results/ge. After a round, X goes back to the menu: for another round, the same one again (which runs only
//what it hasn't finished: see results.c), starting afresh, or leaving.

#include "measure.h"
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PSPMEASURE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);  //the VFPU's tests need it switched on for this thread

//Every button there is: a choice waits for all of them to be let go.
enum {
  AllButtons = PSP_CTRL_SELECT | PSP_CTRL_START | PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT |
               PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS |
               PSP_CTRL_SQUARE,
};

//Waits until every button is let go, so that one still held (the one that made the last choice, say) isn't taken
//again, then until one of these buttons is pressed. Returns the ones that were.
static unsigned int choose(unsigned int buttons) {
  SceCtrlData pad;
  do sceCtrlReadBufferPositive(&pad, 1); while(pad.Buttons & AllButtons);
  do sceCtrlReadBufferPositive(&pad, 1); while(!(pad.Buttons & buttons));
  return pad.Buttons & buttons;
}

//The menu's lines, what each runs, and what to know while it runs (shown under its heading).
enum { Vfpu, Ge, Afresh, Leave };
typedef struct { const char* text; int what, round; const char* note; } Choice;
static const Choice choices[] = {
  {"Round 3: the VFPU and the FPU (about 6 MB)", Vfpu, 3, 0},
  {"The FPU probes (one may switch the PSP off: see below)", Vfpu, 4,
   "If one switches the PSP off, start this again and choose the\n"
   "probes again: it gives up on that one and goes on with the next.\n"},
  {"Round 2 again: the VFPU and the FPU (about 230 MB)", Vfpu, 2, 0},
  {"Round 2 again: the GE and the controller (about 15 MB)", Ge, 2, 0},
  {"Round 1 again: the VFPU (about 450 MB)", Vfpu, 1,
   "Its random number test (vrnd) runs once per start: if round 1 has\n"
   "already run since starting, start the program again for it.\n"},
  {"Start afresh (the results so far are kept: see below)", Afresh, 0, 0},
  {"Leave", Leave, 0, 0},
};
enum { ChoiceCount = sizeof(choices) / sizeof(choices[0]) };

//The menu, the chosen line marked. The screen is 68 characters wide, and a line of exactly 68 is followed by an
//empty one, so every line here is shorter.
static void showMenu(int chosen) {
  pspDebugScreenClear();
  print("psp-measure: what this PSP's VFPU, FPU and GE compute and draw\n");
  print("Up and down choose, X runs.\n\n");
  for(int n = 0; n < ChoiceCount; n++) print("%s %s\n", n == chosen ? ">" : " ", choices[n].text);
  print("\nResults go to results/ beside EBOOT.PBP: the VFPU's and FPU's in\n"
        "vfpu/, the GE's in ge/. A round skips the tests it has finished,\n"
        "so it can be stopped and started again. A test that stops the PSP\n"
        "runs once more, then is given up on; a probe is given up on at\n"
        "once (start this again and choose the probes again: it goes on\n"
        "with the next). Start afresh renames results to results-1 (or the\n"
        "next number free), so every test runs again. Nothing is deleted.\n");
}

//Renames the results folder results-1 (or the first number that's free) and makes a new, empty one, so every test
//runs again; nothing is deleted. It asks first.
static void startAfresh(void) {
  print("Start afresh? The results so far are kept, as results-1 (or the\n"
        "next number free), and every test runs again.\n"
        "X: yes. Any other button: no.\n\n");
  if(!(choose(AllButtons) & PSP_CTRL_CROSS)) {
    print("Left as they were.\n");
    return;
  }
  char kept[272];
  int number = 1;
  do snprintf(kept, sizeof(kept), "%s-%d", results, number++); while(exists(kept) && number <= 999);
  if(sceIoRename(results, kept) < 0) {
    print("Can't rename %s (memory stick?): left as it was.\n", results);
    return;
  }
  sceIoMkdir(results, 0777);
  print("The results so far are in %s, and results is empty.\n", strrchr(kept, '/') + 1);
}

int main(int argc, char** argv) {
  vfpuStart();
  pspDebugScreenInit();
  findResults(argc > 0 ? argv[0] : 0);

  if(Smoke) {  //the quick version for emulators: round 3's VFPU and FPU tests, the probes, the GE's; then it leaves
    print("writing to %s\n\n", results);
    vfpuRound(3);
    vfpuRound(4);
    geRound(2);
    geEnd();
    sceKernelExitGame();
    return 0;
  }

  int chosen = 0;
  for(;;) {
    showMenu(chosen);
    unsigned int pressed = choose(PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_CROSS);
    if(pressed & PSP_CTRL_UP) chosen = (chosen + ChoiceCount - 1) % ChoiceCount;
    else if(pressed & PSP_CTRL_DOWN) chosen = (chosen + 1) % ChoiceCount;
    else {
      const Choice* choice = &choices[chosen];
      if(choice->what == Leave) break;
      pspDebugScreenClear();  //each run's lines start at the top
      if(choice->what == Afresh) startAfresh();
      else {
        print("%s\nwriting to %s/%s\n\n", choice->text, results, choice->what == Vfpu ? "vfpu" : "ge");
        if(choice->note) print("%s\n", choice->note);
        int ok = choice->what == Vfpu ? vfpuRound(choice->round) : geRound(choice->round);
        print(ok ? "\nAll done.\n" : "\nSomething couldn't be written (memory stick full?): see above.\n");
      }
      print("\nPress X for the menu.\n");
      choose(PSP_CTRL_CROSS);
    }
  }
  geEnd();
  sceKernelExitGame();
  return 0;
}
