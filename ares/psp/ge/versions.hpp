#pragma once

#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

//Versions of memory's bytes (option "GE Thread": ge/thread.hpp, docs/psp-core.md): the emulation thread never waits
//for the GE thread's reads or write-backs at earlier places in the stream. Before it writes bytes on a page the GE
//thread may still read or write back (G-interest), it saves their old contents with the write's place, a version:
//  1. the version is published on the page (sequentially consistent);
//  2. it waits till nothing of the GE thread's holds the page (its pin);
//  3. it copies the bytes into the version and marks it ready;
//  4. it writes memory.
//The GE thread reads and writes back as of its own place q: it pins the pages (sequentially consistent), finds the
//versions newer than q over them (at or after q, writing back: a finish a replayed access makes comes before that
//access's write), takes each byte from the earliest that has it, or memory where none has, then unpins. It never
//waits while it holds a pin: a version not yet ready has it let go of every pin, wait till the version is, and start
//again (R13). Either the GE thread sees the version, or the emulation thread sees the pin and waits for that access
//to end, and its copy, taken after, has any write-back the GE thread made under the pin: the two never touch the same
//bytes at once. The emulation thread waits only for one page's pin at a time, which the GE thread lets go of within
//one access; the GE thread waits, holding no pin, only for a version the emulation thread has published and will make
//ready once that pin is clear. A version is freed once the GE thread has passed its place. (The emulation thread
//makes a version ready, and writes memory, before it appends the write's record: it never waits for room in the ring
//in between.) Write-backs store only the bytes that change: an unchanged byte written while the emulation thread
//reads it would race, however harmlessly.

namespace ares::PlayStationPortable {

struct GeVersions {
  static constexpr u32 PageBits = 12, PageBytes = 1u << PageBits, Pages = 512_MiB / PageBytes;

  //Bytes [offset, offset + size) of its page as they were before the write at place at.
  struct Version {
    u64 at = 0;
    u32 offset = 0, size = 0;
    std::atomic<bool> ready{false};
    std::atomic<Version*> next{nullptr};  //(the page's next newer one)
    std::vector<u8> bytes;
  };
  struct Page {
    std::atomic<u32> pins{0};
    std::atomic<Version*> first{nullptr};  //(the oldest)
    Version* last = nullptr;               //(the emulation thread's: the newest)
  };

  //host(page): where the page (an address's low 29 bits over 4 KiB, VRAM by its first copy's) is in the host's
  //memory, or nullptr.
  using Host = u8* (*)(void* context, u32 page);
  GeVersions(Host host, void* context) : host(host), context(context), pages(new Page[Pages]) {}
  ~GeVersions();

  //(the emulation thread's) The old contents of size bytes of the page from offset (inside it) saved as of place at,
  //before it writes them.
  auto save(u32 page, u32 offset, u32 size, u64 at) -> void;
  //(the emulation thread's) The versions the GE thread has passed (at places up to passed) freed.
  auto free(u64 passed) -> void;
  auto versions() const -> u64 { return live.size(); }

  //(the GE thread's) Pages held while it reads them as of place q (write: or writes them back), the pages first to
  //last and first2 to last2 (none: first past last), pinned as it's made and let go of as it goes. versioned: whether
  //any version is to be taken over them; where none is, memory is theirs to read and write back as it is.
  struct Hold {
    Hold(GeVersions& versions, u64 q, bool write, u32 first, u32 last, u32 first2 = 1, u32 last2 = 0);
    ~Hold();
    Hold(const Hold&) = delete;
    auto operator=(const Hold&) -> Hold& = delete;
    //The page's size bytes from offset as of q into out, from the versions and memory.
    auto read(u32 page, u32 offset, u32 size, u8* out) const -> void;
    //in's size bytes into the page from offset as of q: each that changes into the earliest version at or after q
    //that has it, else memory (stored(byte) told first of each of those, where it's given).
    auto writeBack(u32 page, u32 offset, u32 size, const u8* in,
                   const std::function<auto (u32 byte) -> void>* stored = nullptr) const -> void;
    GeVersions& v;
    u64 q;
    bool write;
    bool versioned = false;
    u32 ranges[2][2];
  private:
    auto pin(s32 delta) const -> void;
    auto applies(const Version& version) const -> bool { return write ? version.at >= q : version.at > q; }
  };

  Host host;
  void* context;
  //(the tests' hooks: the emulation thread between publishing a version and looking at the pin, the GE thread between
  //pinning and looking for versions)
  std::function<auto () -> void> published, pinned;
  std::unique_ptr<Page[]> pages;
  std::deque<std::pair<u32, Version*>> live;  //(the emulation thread's: every version made, oldest first, its page)
  std::vector<Version*> spare;                //(the emulation thread's: freed, to be used again)
};

}
