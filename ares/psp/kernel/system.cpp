//Leaving the program, and the odds and ends a C library's start-up asks the system for.

//The program is over: back to the PSP's menu (here: the system stops running it).
auto Kernel::sceKernelExitGame() -> void {
  exited = true;
  switchTo(nullptr);
}

//(exit status, argument size, argument): the module stops and unloads itself, which for a program on its own is
//the same as leaving.
auto Kernel::sceKernelSelfStopUnloadModule() -> void {
  exited = true;
  switchTo(nullptr);
}

//(which setting, where to put it): the system's settings (psputility_sysparam.h). Language 1 is English; button
//swap 1 means the cross button confirms, as outside Japan; anything else reads 0.
auto Kernel::sceUtilityGetSystemParamInt() -> void {
  u32 which = arg(0), value = 0;
  if(which == 8) value = 1;  //PSP_SYSTEMPARAM_ID_INT_LANGUAGE
  if(which == 9) value = 1;  //PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP
  if(arg(1)) memory.write(4, arg(1), value);
  result(0);
}

//No network yet.
auto Kernel::sceNetInetUnavailable() -> void {
  result(0xffff'ffff);
}
