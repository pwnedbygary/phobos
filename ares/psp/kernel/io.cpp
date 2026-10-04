//Standard input, output and error. A program's printf ends up in sceIoWrite to standard output, which goes to
//output(). Files and folders come later: until then opening anything says it isn't there.

enum : u32 { StandardInput = 0, StandardOutput = 1, StandardError = 2 };

auto Kernel::sceKernelStdin() -> void { result(StandardInput); }
auto Kernel::sceKernelStdout() -> void { result(StandardOutput); }
auto Kernel::sceKernelStderr() -> void { result(StandardError); }

//(file, data, size): how many bytes were written.
auto Kernel::sceIoWrite() -> void {
  u32 file = arg(0), data = arg(1), size = arg(2);
  if(file != StandardOutput && file != StandardError) return result(ErrorBadFile);
  size = std::min<u32>(size, 64_KiB);
  std::string text(size, '\0');
  if(!memory.copyOut(text.data(), data, size)) return result(ErrorIllegalAddress);
  if(output) output(text);
  result(size);
}

//Standard input has nothing to read.
auto Kernel::sceIoRead() -> void {
  result(arg(0) == StandardInput ? 0 : ErrorBadFile);
}

auto Kernel::sceIoClose() -> void {
  result(arg(0) <= StandardError ? 0 : ErrorBadFile);
}

//(file, 64-bit offset in a2 and a3, whence): the new position, in v0 and v1.
auto Kernel::sceIoLseek() -> void {
  cpu.ipu.r[3] = arg(0) <= StandardError ? 0 : 0xffff'ffff;
  result(arg(0) <= StandardError ? 0 : ErrorBadFile);
}

auto Kernel::sceIoOpen() -> void {
  note("no files yet: " + memory.readString(arg(0), 256) + " isn't there");
  result(ErrorFileNotFound);
}

auto Kernel::sceIoDopen() -> void {
  note("no files yet: the folder " + memory.readString(arg(0), 256) + " isn't there");
  result(ErrorFileNotFound);
}

auto Kernel::sceIoDread() -> void { result(ErrorBadFile); }
auto Kernel::sceIoDclose() -> void { result(ErrorBadFile); }

//The folder relative paths start from.
auto Kernel::sceIoChdir() -> void {
  workingDirectory = memory.readString(arg(0), 256);
  result(0);
}

auto Kernel::sceIoGetstat() -> void {
  result(ErrorFileNotFound);
}
