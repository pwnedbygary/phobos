#pragma once

#include <functional>
#include <memory>
#include <nall/file.hpp>
#include <nall/maybe.hpp>
#include <nall/string.hpp>
#if defined(ARES_ENABLE_CHD)
#include <libchdr/chd.h>
#endif

namespace nall::Decode {

//A disc image in MAME's CHD form ("compressed hunks of data"), read with libchdr: the disc cut into hunks, each
//packed on its own. Two kinds of disc: a CD's (chdman createcd), its tracks listed in the image's metadata and each
//sector stored as a 2352-byte frame and 96 bytes of subchannel data; or a DVD's (chdman createdvd), as a PSP's UMD
//is stored, no tracks, just 2048-byte sectors one after another.
struct CHD {
  ~CHD();
  struct Index {
    auto sectorCount() const -> u32;

    u8 number = 0xff; //00-99
    s32 lba = -1;
    s32 end = -1;
    s32 chd_lba = -1;
  };

  struct Track {
    auto sectorCount() const -> u32;

    u8 number = 0xff; //01-99
    string type;
    std::vector<Index> indices;
    maybe<s32> pregap;
    maybe<s32> postgap;
  };

  //Reads size bytes from offset in the image into data; returns how many it got.
  using Reader = std::function<u64 (u64 offset, void* data, u64 size)>;

  auto load(const string& location) -> bool;
  //An image read through a reader rather than opened by its name: a file a front end holds open, as Android's file
  //picker hands over a descriptor whose file the app may have no permission to open again by its path.
  auto load(Reader reader, u64 size) -> bool;
  //A CD's frame at a disc address (2352 bytes, or 2048 in a MODE1 track), or a DVD's 2048-byte sector; empty if it
  //isn't on the disc, or (a DVD's) its hunk is damaged.
  auto read(u32 sector) const -> std::vector<u8>;
  auto sectorCount() const -> u32;
  auto dvd() const -> bool { return isDVD; }

  std::vector<Track> tracks;  //a CD's
  string error;               //why load() failed
private:
  auto loadHeader() -> bool;

  //what load(reader, size) was given, and where libchdr's next read starts: kept apart from the CHD so its address,
  //which libchdr holds, stays put even if the CHD is moved
  struct Source {
    Reader reader;
    u64 size = 0;
    u64 position = 0;
  };

  file_buffer fp;
  std::unique_ptr<Source> source;
  chd_file* chd = nullptr;
  static constexpr int chd_sector_size = 2352 + 96;
  size_t chd_hunk_size;
  mutable std::vector<u8> chd_hunk_buffer;
  mutable int chd_current_hunk = -1;
  bool isDVD = false;
  u64 dvdBytes = 0;  //a DVD's size: its "logical" bytes, up to which the hunks hold the disc
};

inline CHD::~CHD() {
  if (chd != nullptr) {
     chd_close(chd);
  }
}

inline auto CHD::load(const string& location) -> bool {
  fp = file::open(location, file::mode::read);
  if(!fp) {
    error = "the file can't be opened";
    print("CHD: Failed to open ", location, "\n");
    return false;
  }

  chd_error err = chd_open_file(fp.handle(), CHD_OPEN_READ, nullptr, &chd);
  if (err != CHDERR_NONE) {
    chd = nullptr;
    error = chd_error_string(err);
    print("CHD: Failed to open ", location, ": ", chd_error_string(err), "\n");
    return false;
  }
  return loadHeader();
}

inline auto CHD::load(Reader reader, u64 size) -> bool {
  source = std::make_unique<Source>(Source{std::move(reader), size});
  //libchdr's way into the file: its size, reads from where the last seek left off, and seeks. Closing is left to
  //whoever holds the file.
  static const core_file_callbacks callbacks = {
    [](void* argp) -> uint64_t { return ((Source*)argp)->size; },
    [](void* buffer, size_t size, size_t count, void* argp) -> size_t {
      auto& source = *(Source*)argp;
      if(!size || count > ~size_t(0) / size || source.position >= source.size) return 0;
      u64 wanted = std::min<u64>(u64(size) * count, source.size - source.position);
      u64 got = source.reader(source.position, buffer, wanted);
      source.position += got;
      return got / size;
    },
    [](void*) -> int { return 0; },
    [](void* argp, int64_t offset, int whence) -> int {
      auto& source = *(Source*)argp;
      s64 base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? s64(source.position) : s64(source.size);
      if(offset < -base) return -1;
      source.position = u64(base + offset);
      return 0;
    },
  };
  chd_error err = chd_open_core_file_callbacks(&callbacks, source.get(), CHD_OPEN_READ, nullptr, &chd);
  if(err != CHDERR_NONE) {
    chd = nullptr;
    error = err == CHDERR_REQUIRES_PARENT ? "it only holds its differences from another CHD" : chd_error_string(err);
    print("CHD: Failed to open: ", error, "\n");
    return false;
  }
  return loadHeader();
}

//What the image holds: a DVD's hunks of whole sectors, or a CD's frames in the tracks its metadata lists.
inline auto CHD::loadHeader() -> bool {
  const chd_header* header = chd_get_header(chd);
  chd_hunk_size = header->hunkbytes;

  if (header->unitbytes == 2048) {
    if (!chd_hunk_size || chd_hunk_size % 2048 || u64(header->totalhunks) * chd_hunk_size < header->logicalbytes) {
      error = "its hunks don't hold whole sectors of the disc";
      print("CHD: a DVD's hunk size (", chd_hunk_size, ") doesn't hold its sectors\n");
      return false;
    }
    isDVD = true;
    dvdBytes = header->logicalbytes;
    chd_hunk_buffer.resize(chd_hunk_size);
    return true;
  }

  if ((chd_hunk_size % chd_sector_size) != 0) {
    error = "its hunks don't hold whole frames of a CD";
    print("CHD: hunk size (", chd_hunk_size, ") is not a multiple of ", chd_sector_size, "\n");
    return false;
  }

  chd_hunk_buffer.resize(chd_hunk_size);
  u32 disc_lba = 0;
  u32 chd_lba = 0;
  chd_error err;

  // Fetch track structure
  while(true) {
    char metadata[256];
    char type[256];
    char subtype[256];
    char pgtype[256];
    char pgsub[256];
    u32 metadata_size;

    int track_no;
    int frames;
    int pregap_frames = 0;
    int postgap_frames = 0;

    // First, attempt to fetch CDROMv2 metadata
    err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, tracks.size(), metadata, sizeof(metadata), &metadata_size, nullptr, nullptr);
    if (err == CHDERR_NONE) {
      if (std::sscanf(metadata, CDROM_TRACK_METADATA2_FORMAT, &track_no, type, subtype, &frames, &pregap_frames, pgtype, pgsub, &postgap_frames) != 8) {
        print("CHD: Invalid track v2 metadata: ", metadata,  "\n");
        return false;
      }
    } else {
      // That failed, so try to fetch CDROM (old) metadata
      err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, tracks.size(), metadata, sizeof(metadata),  &metadata_size, nullptr, nullptr);
      if (err != CHDERR_NONE) {
        // Both meta-data types failed to fetch, so assume there are no further tracks
        break;
      }

      if (std::sscanf(metadata, CDROM_TRACK_METADATA_FORMAT, &track_no, type, subtype, &frames) != 4) {
        print("CHD: Invalid track metadata: ", metadata, "\n");
        return false;
      }
    }

    // We currently only support RAW and audio tracks; log an error and exit if we see anything different
    auto typeStr = string{type};
    if (!(typeStr.find("_RAW") || typeStr.find("AUDIO") || typeStr.find("MODE1"))) {
      print("CHD: Unsupported track type: ", type, "\n");
      return false;
    }

    const bool pregap_in_file = (pregap_frames > 0 && pgtype[0] == 'V');
    const int track1_pregap = (track_no == 1 && !pregap_in_file) ? 2 * 75 : 0;

    // Add the new track
    Track track;
    track.number = track_no;
    track.type = type;
    track.pregap = pregap_frames;
    track.postgap = postgap_frames;

    // index0 = Pregap
    if (pregap_frames > 0 || track1_pregap > 0) {
      Index index;
      index.number = 0;

      if (pregap_in_file) {
        index.lba = disc_lba;
        index.end = disc_lba + pregap_frames - 1;
        index.chd_lba = chd_lba;

        if (pregap_frames > frames) {
          print("CHD: pregap length ", pregap_frames, " exceeds track length ", frames, "\n");
          return false;
        }

        disc_lba += pregap_frames;
        chd_lba  += pregap_frames;
        frames   -= pregap_frames;

        track.indices.push_back(index);
      } else if(track_no == 1 && track1_pregap) {
        index.lba = -track1_pregap;
        index.end = -1;
        index.chd_lba = -1;
        track.indices.push_back(index);
      } else if(pregap_frames > 0) {
        //a pregap left out of the image (PREGAP without a 'V' PGTYPE) is silence on the disc, and the
        //tracks after it start that much later, as with a cue sheet's PREGAP; games seek to those positions
        index.lba = disc_lba;
        index.end = disc_lba + pregap_frames - 1;
        index.chd_lba = -1;
        disc_lba += pregap_frames;
        track.indices.push_back(index);
      }
    }

    // index1 = track data
    {
      Index index;
      index.number = 1;
      index.lba = disc_lba;
      index.end = disc_lba + frames - 1;
      index.chd_lba = chd_lba;
      track.indices.push_back(index);
      disc_lba += frames;
      chd_lba += frames;

      // chdman pads each track to a 4-frame boundary
      chd_lba = (chd_lba + 3) / 4 * 4;
    }

    // index2 = postgap
    if (postgap_frames > 0) {
      Index index;
      index.number = 2;
      index.lba = disc_lba;
      index.end = disc_lba + postgap_frames - 1;
      index.chd_lba = -1;
      track.indices.push_back(index);
      disc_lba += postgap_frames;
    }

    tracks.push_back(track);
  }

  return true;
}

inline auto CHD::read(u32 sector) const -> std::vector<u8> {
  if (isDVD) {
    u64 offset = u64(sector) * 2048;
    if (offset >= dvdBytes) return {};
    int hunk = int(offset / chd_hunk_size);
    if (hunk != chd_current_hunk) {
      chd_current_hunk = -1;  //whatever happens below, the buffer won't hold the hunk it held
      if (chd_read(chd, hunk, chd_hunk_buffer.data()) != CHDERR_NONE) return {};
      chd_current_hunk = hunk;
    }
    auto start = chd_hunk_buffer.begin() + offset % chd_hunk_size;
    return {start, start + 2048};
  }

  // Convert LBA in CD-ROM to LBA in CHD
  for(auto& track : tracks) {
    for(auto& index : track.indices) {
      if (sector >= index.lba && sector <= index.end) {
        if (index.chd_lba < 0) return {};
        auto chd_lba = (sector - index.lba) + index.chd_lba;

        std::vector<u8> output;
        output.resize(track.type == "MODE1" ? 2048 : 2352);

        int hunk = (chd_lba * chd_sector_size) / chd_hunk_size;
        int offset = (chd_lba * chd_sector_size) % chd_hunk_size;

        if (hunk != chd_current_hunk) {
          chd_read(chd, hunk, chd_hunk_buffer.data());
          chd_current_hunk = hunk;
        }

        // Audio data is in big-endian, so we need to byteswap
        if (track.type == "AUDIO") {
          u8* src_ptr = chd_hunk_buffer.data() + offset;
          u8* dst_ptr = output.data();
          const int value_count = 2352 / sizeof(uint16_t);
          for (int i = 0; i < value_count; i++) {
            u16 value;
            memcpy(&value, src_ptr, sizeof(value));
            value = (value << 8) | (value >> 8);
            memcpy(dst_ptr, &value, sizeof(value));
            src_ptr += sizeof(value);
            dst_ptr += sizeof(value);
          }
        } else {
          std::copy(chd_hunk_buffer.data() + offset, chd_hunk_buffer.data() + offset + output.size(), output.data());
        }

        return output;
      }
    }
  }

  print("CHD: Attempting to read from unmapped sector ", sector, "\n");
  return {};
}

inline auto CHD::sectorCount() const -> u32 {
  if (isDVD) return u32((dvdBytes + 2047) / 2048);
  u32 count = 0;
  for(auto& track : tracks) count += track.sectorCount();
  return count;
}

inline auto CHD::Track::sectorCount() const -> u32 {
  u32 count = 0;
  for(auto& index : indices) count += index.sectorCount();
  return count;
}

inline auto CHD::Index::sectorCount() const -> u32 {
  if(end < 0) return 0;
  return end - lba + 1;
}

}
