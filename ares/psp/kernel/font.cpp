//sceLibFont, the system's font library, which draws the PSP's own fonts (flash0:/font, the PGF files) for games
//that print with them. Those files are the PSP's firmware, which Phobos doesn't have (they'll come from the owner's
//own PSP's flash0), so here the library starts and finds no fonts installed: sceFontNewLib gives a library, which
//lists none and opens none; asking for a font is refused with an error, as is anything a font would answer. Games
//that print with the system's fonts then print nothing; the ones tried go on past it (Gunhound EX, Space Invaders
//Extreme). The functions' arguments are as the homebrew scene's headers for the library describe them (it isn't in
//pspsdk); which error a PSP gives for a font it hasn't got isn't known here: NOT_FOUND (uOFW's errors.h) is written
//where the call takes an error's address, and returned where it returns one. Positions in points and pixels are
//worked out at the library's resolution (128 dots an inch, until sceFontSetResolution changes it: a font's size in
//points makes 128/72 times as many pixels).

namespace {
  constexpr u32 FontLibrary = 0x0000'f001;  //the handle sceFontNewLib gives (nothing a game reads through)
}

//(parameters, where to put an error): the library, with no fonts; the error 0.
auto Kernel::sceFontNewLib() -> void {
  if(arg(1)) memory.write(4, arg(1), 0);
  result(FontLibrary);
}

//(library): 0.
auto Kernel::sceFontDone() -> void {
  result(0);
}

//(library, where to put an error): how many fonts are installed: none.
auto Kernel::sceFontGetNumFontList() -> void {
  if(arg(1)) memory.write(4, arg(1), 0);
  result(0);
}

//(library, where to put the styles, how many it has room for): none written; 0.
auto Kernel::sceFontGetFontList() -> void {
  result(0);
}

//(library, a style wanted, where to put an error): no font fits it: -1, and the error.
auto Kernel::sceFontFindOptimumFont() -> void {
  if(arg(2)) memory.write(4, arg(2), ErrorNotFound);
  result(0xffff'ffff);
}

//(library, index or what to open it from, mode, where to put an error): no font: 0, and the error. (The four ways to
//open one: an index, a style, the program's memory, a file.)
auto Kernel::sceFontOpen() -> void {
  if(arg(3)) memory.write(4, arg(3), ErrorNotFound);
  result(0);
}

//What only an open font answers (its details, a character's, its glyph images): there's none.
auto Kernel::sceFontNoFont() -> void {
  result(ErrorNotFound);
}

//(library, horizontal and vertical resolution, floats in f12 and f13): the dots an inch the library works in.
auto Kernel::sceFontSetResolution() -> void {
  memcpy(&fontResolution[0], &cpu.fpu.r[12], 4);
  memcpy(&fontResolution[1], &cpu.fpu.r[13], 4);
  if(!(fontResolution[0] > 0) || !(fontResolution[1] > 0)) fontResolution[0] = fontResolution[1] = 128.0f;
  result(0);
}

//(library, a size, a float in f12, where to put an error): points to pixels, or pixels to points, horizontally or
//vertically; a float in f0.
auto Kernel::fontScale(bool toPixels, u32 axis) -> void {
  float value;
  memcpy(&value, &cpu.fpu.r[12], 4);
  if(arg(1)) memory.write(4, arg(1), 0);
  resultFloat(toPixels ? value * fontResolution[axis] / 72.0f : value * 72.0f / fontResolution[axis]);
}

auto Kernel::sceFontPointToPixelH() -> void { fontScale(true, 0); }
auto Kernel::sceFontPointToPixelV() -> void { fontScale(true, 1); }
auto Kernel::sceFontPixelToPointH() -> void { fontScale(false, 0); }
auto Kernel::sceFontPixelToPointV() -> void { fontScale(false, 1); }
