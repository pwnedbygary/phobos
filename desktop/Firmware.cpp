#include "Firmware.hpp"
#include "Platform.hpp"
#include "PhobosRunner.hpp"

#include <nall/hash/crc32.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace phobos::desktop {

// Copies of biosMap, biosAliases, keywordFirmwareMatch() and biosCrcMap in MainViewModel.kt.
static const std::map<std::string, std::string> firmwareByName = {
  {"scph5501.bin", "fw_psx_us"}, {"scph5500.bin", "fw_psx_jp"}, {"scph5502.bin", "fw_psx_eu"},
  {"scph101.bin", "fw_psx_us_v45"},
  {"neogeo.zip", "fw_ng_bios"}, {"aleck64.zip", "fw_aleck64"}, {"aes.zip", "fw_ng_aes"},
  {"neocd.zip", "fw_ng_cd"}, {"neocd.bin", "fw_ng_cd"}, {"ngcd.zip", "fw_ng_cd"}, {"ngcd.bin", "fw_ng_cd"},
  {"ngp.zip", "fw_ngp"}, {"ngpc.zip", "fw_ngpc"}, {"ngp.bin", "fw_ngp"}, {"ngpc.bin", "fw_ngpc"},
  {"neo geo pocket - bios (world).bin", "fw_ngp"},
  {"neo geo pocket color - bios (world).bin", "fw_ngpc"},
  {"64dd_ipl.bin", "fw_n64dd_jp"}, {"n64dd_ipl.bin", "fw_n64dd_jp"}, {"n64dd_ipl_jp.bin", "fw_n64dd_jp"},
  {"n64dd_ipl_us.bin", "fw_n64dd_us"}, {"n64dd_ipl_dev.bin", "fw_n64dd_dev"},
  {"[bios] nintendo 64dd ipl (japan) (v1.0).zip", "fw_n64dd_jp"},
  {"[bios] nintendo 64dd ipl (japan) (v1.2).zip", "fw_n64dd_jp"},
  {"[bios] nintendo 64dd ipl (usa) (proto).zip", "fw_n64dd_us"},
  {"[bios] nintendo 64dd ipl (usa) (proto) (v1.0).zip", "fw_n64dd_us"},
  {"[bios] nintendo 64dd ipl (usa) (proto) (v1.1).zip", "fw_n64dd_us"},
  {"nintendo 64dd ipl (japan).bin", "fw_n64dd_jp"},
  {"nintendo 64dd ipl (usa) (proto).bin", "fw_n64dd_us"},
  {"pif.ntsc.rom", "fw_n64_pif_ntsc"}, {"pif.pal.rom", "fw_n64_pif_pal"},
  {"segacd_usa.bin", "fw_mcd_us"}, {"segacd_jp.bin", "fw_mcd_jp"}, {"segacd_eu.bin", "fw_mcd_eu"},
  {"mcd_v1_10.bin", "fw_mcd_us"}, {"bios_cd_u.bin", "fw_mcd_us"},
  {"mcd_v1_10j.bin", "fw_mcd_jp"}, {"bios_cd_j.bin", "fw_mcd_jp"},
  {"mcd_v1_10e.bin", "fw_mcd_eu"}, {"bios_cd_e.bin", "fw_mcd_eu"},
  {"32x_g_bios.bin", "fw_32x_g"}, {"32x_m_bios.bin", "fw_32x_m"}, {"32x_s_bios.bin", "fw_32x_s"},
  {"sh2.boot.mrom", "fw_32x_m"}, {"sh2.boot.srom", "fw_32x_s"},
  {"syscard1.pce", "fw_pce_cd_1_jp"}, {"syscard3.pce", "fw_pce_cd_3_jp"},
  {"syscard3u.pce", "fw_pce_cd_3_us"}, {"syscard3us.pce", "fw_pce_cd_3_us"},
  {"gexpress.pce", "fw_pce_cd_ge_jp"},
  {"disksys.rom", "fw_fds"}, {"fds.rom", "fw_fds"},
  {"coleco.rom", "fw_coleco"}, {"colecovision.rom", "fw_coleco"},
  {"gba_bios.bin", "fw_gba"},
  {"dmg_boot.bin", "fw_gb_boot"}, {"cgb_boot.bin", "fw_gbc_boot"}, {"sgb_boot.bin", "fw_sgb_boot"},
  {"gb_bios.bin", "fw_gb_boot"}, {"gbc_bios.bin", "fw_gbc_boot"},
  {"sgb.sfc", "fw_sgb1"}, {"sgb.smc", "fw_sgb1"}, {"sgb1.sfc", "fw_sgb1"}, {"sgb1.smc", "fw_sgb1"},
  {"super game boy.sfc", "fw_sgb1"}, {"super game boy.smc", "fw_sgb1"},
  {"sgb2.sfc", "fw_sgb2"}, {"sgb2.smc", "fw_sgb2"},
  {"super game boy 2.sfc", "fw_sgb2"}, {"super game boy 2.smc", "fw_sgb2"},
  {"bios_u.sms", "fw_ms_us"}, {"bios_j.sms", "fw_ms_jp"}, {"bios_e.sms", "fw_ms_eu"},
  {"msx.rom", "fw_msx"}, {"msx2.rom", "fw_msx2_main"}, {"msx2ext.rom", "fw_msx2_sub"},
  {"gg_bios.bin", "fw_gg"}, {"game_gear_bios.bin", "fw_gg"},
  {"scph1000.bin", "fw_psx_jp"}, {"scph1001.bin", "fw_psx_us"}, {"scph7001.bin", "fw_psx_us"},
  {"scph7003.bin", "fw_psx_eu"}, {"scph7502.bin", "fw_psx_eu"},
  {"48.rom", "fw_zx48"}, {"zx48.rom", "fw_zx48"}, {"zxspectrum.rom", "fw_zx48"}, {"spectrum.rom", "fw_zx48"},
  {"zx spectrum 48k.rom", "fw_zx48"}, {"zx spectrum (48k).rom", "fw_zx48"},
  {"[bios] zx spectrum (48k).rom", "fw_zx48"}, {"sinclair zx spectrum.rom", "fw_zx48"},
  {"128-0.rom", "fw_zx128"}, {"128_0.rom", "fw_zx128"}, {"1280.rom", "fw_zx128"}, {"zx128.rom", "fw_zx128"},
  {"zx spectrum 128.rom", "fw_zx128"}, {"[bios] zx spectrum 128.rom", "fw_zx128"},
  {"128-1.rom", "fw_zx128_sub"}, {"128_1.rom", "fw_zx128_sub"}, {"1281.rom", "fw_zx128_sub"},
  {"zx128_sub.rom", "fw_zx128_sub"},
};

static const std::map<std::string, std::vector<std::string>> firmwareAliases = {
  {"fw_ng_bios", {"fw_ng_aes", "fw_ng_mvs"}},
  {"fw_ng_aes", {"fw_ng_bios", "fw_ng_mvs"}},
  {"fw_ng_mvs", {"fw_ng_bios", "fw_ng_aes"}},
};

static const std::map<std::uint32_t, std::string> firmwareByCrc = {
  {0x1105ca35, "fw_psx_us"}, {0xff3d245b, "fw_psx_jp"}, {0x3273398d, "fw_psx_eu"},
  {0x74360e22, "fw_n64dd_jp"}, {0x5ec82be9, "fw_n64_pif_sm5"},
  {0x4353387a, "fw_n64_pif_ntsc"}, {0x59b859e7, "fw_n64_pif_pal"},
  {0x32fce34a, "fw_gb_boot"}, {0xebf565e8, "fw_gbc_boot"}, {0xe03ee2d7, "fw_sgb_boot"},
  {0x3c0c41ef, "fw_coleco"}, {0x6605af34, "fw_coleco"}, {0xff0ecca5, "fw_coleco"},
  {0xc8338226, "fw_coleco"}, {0x32584700, "fw_coleco"}, {0xdf1c9a84, "fw_coleco"},
  {0x924e3926, "fw_psx_us"}, {0x55847d8c, "fw_psx_jp"}, {0xa56e4c9e, "fw_psx_eu"}, {0xf7b04630, "fw_psx_us"},
  {0x48cd46be, "fw_ms_us"}, {0x80eb3c3c, "fw_ms_jp"}, {0xd0569c83, "fw_ms_eu"},
  {0xeecf3fa1, "fw_gg"},
  {0x5c12eae8, "fw_32x_g"}, {0xdd9c46b8, "fw_32x_m"}, {0xbfda1fe5, "fw_32x_s"},
  {0xee229390, "fw_msx"}, {0xfcb98b8a, "fw_msx2_main"}, {0x57798735, "fw_msx2_sub"},
  {0x11726b6d, "fw_ngp"}, {0x6232df8d, "fw_ngp"}, {0xcdc1a5c2, "fw_ngpc"}, {0x6eeb6f40, "fw_ngpc"},
};

static auto contains(const std::string& text, const char* part) -> bool { return text.find(part) != std::string::npos; }

static auto keywordMatch(const std::string& name) -> std::string {
  if (contains(name, "64dd") && contains(name, "ipl")) {
    if (contains(name, "dev")) return "fw_n64dd_dev";
    if (contains(name, "usa") || contains(name, "(us") || contains(name, "proto")) return "fw_n64dd_us";
    return "fw_n64dd_jp";
  }
  if (contains(name, "sgb") && !contains(name, "boot")) {
    if (contains(name, "sgb2") || contains(name, "game boy 2") || contains(name, "gameboy2")) return "fw_sgb2";
    return "fw_sgb1";
  }
  if (contains(name, "super game boy") || contains(name, "supergameboy")) {
    if (contains(name, "2")) return "fw_sgb2";
    return "fw_sgb1";
  }
  return {};
}

// Firmware is small (a Super Game Boy cartridge is the largest at 512 KiB); bigger files are games.
static constexpr std::uintmax_t largestFirmware = 4 * 1024 * 1024;

static auto crcMatch(const fs::path& file) -> std::string {
  std::error_code error;
  auto size = fs::file_size(file, error);
  if (error || size == 0 || size > largestFirmware) return {};
  std::ifstream in(file, std::ios::binary);
  std::vector<std::uint8_t> data((size_t)size);
  if (!in.read((char*)data.data(), (std::streamsize)data.size())) return {};
  auto crc = nall::Hash::CRC32(std::span<const std::uint8_t>(data.data(), data.size())).value();
  auto it = firmwareByCrc.find(crc);
  return it == firmwareByCrc.end() ? std::string{} : it->second;
}

auto mapFirmware(const std::string& folder) -> int {
  ares::setFirmwarePath(folder.c_str());
  std::error_code error;
  fs::path root = toPath(folder);
  if (!fs::is_directory(root, error)) return 0;
  int matched = 0;
  for (auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, error)) {
    if (!entry.is_regular_file(error)) continue;
    auto path = fromPath(entry.path());
    auto name = fromPath(entry.path().filename());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::string key;
    bool withAliases = true;
    if (auto it = firmwareByName.find(name); it != firmwareByName.end()) {
      key = it->second;
    } else if (key = keywordMatch(name); key.empty()) {
      key = crcMatch(entry.path());
      withAliases = false;
    }
    if (key.empty()) continue;
    ares::mapFirmwareFile(key.c_str(), path.c_str());
    if (withAliases) {
      if (auto it = firmwareAliases.find(key); it != firmwareAliases.end()) {
        for (auto& alias : it->second) ares::mapFirmwareFile(alias.c_str(), path.c_str());
      }
    }
    matched++;
  }
  return matched;
}

auto firmwareName(const std::string& key) -> std::string {
  if (key == "fw_mcd") return "Mega CD BIOS";
  if (key == "fw_sgb") return "Super Game Boy cartridge";
  return key;
}

}
