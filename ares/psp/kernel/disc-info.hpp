#pragma once

#include <string>
#include <vector>

#include <nall/vfs.hpp>

#include "disc.hpp"

namespace ares::PlayStationPortable {

//What a PSP disc carries for the front ends to show it: its title, its disc ID (its NPD ID, like ULUS10025), the
//region that ID names, and its icon (PSP_GAME/ICON0.PNG). A disc that's damaged, or isn't a PSP game's, leaves its
//fields empty.
struct DiscInfo {
  std::string title;      //PARAM.SFO's TITLE
  std::string discId;     //PARAM.SFO's DISC_ID
  std::string region;     //from the disc ID's prefix
  std::vector<u8> icon;   //PSP_GAME/ICON0.PNG
};

//A PSP disc's title, disc ID, region and icon, read off its image: an ISO, one of the scene's compressed forms
//(CSO, ZSO, DAX, JSO), or a CHD, recognized by its head. It reads a little at a time (a few sectors, through the
//Reader), never the whole image, and bounds every size it finds: a PSP_GAME/PARAM.SFO over 64 KiB, or an
//PSP_GAME/ICON0.PNG over 1 MiB, is taken as none. Its fields are empty when there's no PSP game.
auto readDiscInfo(Disc::Reader reader, u64 imageSize, std::string& error) -> DiscInfo;

//A text value from a PARAM.SFO (its "\0PSF" head, its key and its value tables), by its key's name; empty if the
//key isn't there.
auto sfoValue(const std::vector<u8>& sfo, const char* name) -> std::string;

//An EBOOT.PBP's PARAM.SFO (its title and details): the PBP's first part, from its head's offsets; empty if there's
//none.
auto paramSFO(vfs::file& file) -> std::vector<u8>;

//The region a disc ID (its NPD ID) names: Japan's discs carry a "J", the US's a "U" and Europe's an "E" in the ID's
//third letter; the rest are "Unknown".
auto regionFromDiscId(const std::string& discId) -> std::string;

}
