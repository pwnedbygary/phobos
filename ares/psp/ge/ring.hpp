#pragma once

#include <atomic>
#include <cstring>
#include <vector>

//The GE thread's records (ge/thread.cpp): one writer, the emulation thread's control pass, appends them in the
//stream's order; one reader, the GE thread, takes them in that order. A ring of bytes, each record its size first
//(a multiple of 8) and then its own; a record that won't fit before the ring's end is put at its start, a size of 0
//left where it would have gone. Each side waits on the other's count of bytes (C++20's atomic waits): the reader
//for records, the writer for room. The writer's appends are seen by the reader once published (release, acquire).

namespace ares::PlayStationPortable {

struct RecordRing {
  explicit RecordRing(u32 bytes = 4 << 20) : ring(bytes) {}

  //(the writer) Room for a record of size bytes, waiting for the reader to make it; then publish() hands it over.
  auto reserve(u32 size) -> u8* {
    size = (size + sizeof(u32) + 7) & ~7u;
    if(size > ring.size() / 2) return nullptr;  //(the caller's records stay far smaller)
    u64 at = written;
    u32 offset = at % ring.size();
    if(offset + size > ring.size()) {  //(the rest of the ring left empty: a size of 0 says so)
      roomFor(at + (ring.size() - offset) + size);
      std::memset(&ring[offset], 0, sizeof(u32));
      at += ring.size() - offset, offset = 0;
    } else {
      roomFor(at + size);
    }
    reserved = at, reservedSize = size;
    std::memcpy(&ring[offset], &size, sizeof(u32));
    return &ring[offset + sizeof(u32)];
  }
  auto publish() -> void {
    written = reserved + reservedSize;
    published.store(written, std::memory_order_release);
    wakes.fetch_add(1, std::memory_order_release);
    wakes.notify_one();
  }
  //(whether reserve() takes a record of size bytes: none past half the ring)
  auto fits(u32 size) const -> bool { return ((size + sizeof(u32) + 7) & ~7u) <= ring.size() / 2; }
  //Whether the reader has taken everything published; the bytes published and not yet taken, of the ring's size.
  auto empty() const -> bool { return taken.load(std::memory_order_acquire) == published.load(std::memory_order_acquire); }
  auto backlog() const -> u64 {
    return published.load(std::memory_order_acquire) - taken.load(std::memory_order_acquire);
  }
  auto capacity() const -> u64 { return ring.size(); }

  //(the reader) The next record's bytes, waiting for one; then release() frees its room. nullptr once stop() is called
  //with nothing left.
  auto next() -> const u8* {
    while(true) {
      u32 seen = wakes.load(std::memory_order_acquire);
      u64 at = read, end = published.load(std::memory_order_acquire);
      if(at == end) {
        if(stopping.load(std::memory_order_acquire)) return nullptr;
        wakes.wait(seen, std::memory_order_acquire);
        continue;
      }
      u32 offset = at % ring.size(), size;
      std::memcpy(&size, &ring[offset], sizeof(u32));
      if(!size) {  //(the rest of the ring, skipped)
        read = at + (ring.size() - offset);
        continue;
      }
      current = size;
      return &ring[offset + sizeof(u32)];
    }
  }
  auto release() -> void {
    read += current;
    taken.store(read, std::memory_order_release);
    taken.notify_one();
  }
  //Wakes a reader waiting for records, which then finds none: next() returns nullptr.
  auto stop() -> void {
    stopping.store(true, std::memory_order_release);
    wakes.fetch_add(1, std::memory_order_release);
    wakes.notify_all();
  }

private:
  auto roomFor(u64 end) -> void {
    while(true) {
      u64 freed = taken.load(std::memory_order_acquire);
      if(end - freed <= ring.size()) return;
      taken.wait(freed, std::memory_order_acquire);
    }
  }

  std::vector<u8> ring;
  //(the writer's)
  u64 written = 0, reserved = 0;
  u32 reservedSize = 0;
  //(the reader's)
  u64 read = 0;
  u32 current = 0;
  //(shared)
  alignas(64) std::atomic<u64> published{0};
  alignas(64) std::atomic<u64> taken{0};
  std::atomic<u32> wakes{0};  //(goes up as records are published, and at stop(): what the reader waits on)
  std::atomic<bool> stopping{false};
};

}
