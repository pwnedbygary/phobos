#pragma once
#include <string>

namespace phobos::desktop {

// Hands the runner the firmware files in `folder` under its firmware keys (fw_psx_us,
// fw_mcd_jp...), matched as the Android app's scanFirmware() matches them: by file name, by
// keyword, then by CRC32. Returns how many files matched.
auto mapFirmware(const std::string& folder) -> int;

// A readable name for a key missingFirmware() reports ("fw_mcd" -> "Mega CD BIOS").
auto firmwareName(const std::string& key) -> std::string;

}
