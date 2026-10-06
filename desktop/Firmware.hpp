#pragma once
#include <string>

namespace phobos::desktop {

// Hands the runner the firmware files in `folder` under its firmware keys (fw_psx_us,
// fw_mcd_jp...), matched as the Android app's scanFirmware() matches them: by file name, by
// keyword, then by CRC32. Returns how many files matched.
auto mapFirmware(const std::string& folder) -> int;

// A readable name for a key missingFirmware() reports ("fw_mcd" -> "Mega CD BIOS").
auto firmwareName(const std::string& key) -> std::string;

// The PSP's system fonts in a folder the user picked from a dump of their own PSP's flash0: the
// folder itself (the font folder picked), else its "font" (flash0 picked), else its "flash0/font"
// (the dump picked), whichever first holds one, as the Android app's PspFonts.fontFolder() looks.
// Empty when none does. The PSP core reads them from there as a game starts.
auto pspFontFolder(const std::string& picked) -> std::string;

// How many of the PSP's eighteen fonts (jpn0, ltn0 to ltn15 and kr0 .pgf) `folder` holds.
auto pspFontCount(const std::string& folder) -> int;

}
