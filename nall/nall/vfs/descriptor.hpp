#pragma once

#include <nall/intrinsics.hpp>

#if defined(API_POSIX)
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nall::vfs {

//A file read through a descriptor someone else opened rather than through its path: Android hands a file the user
//picked over that way, and opening its path again may be refused. It keeps a descriptor of its own, so it stays
//readable after the one it was given is closed. It maps the file into memory when asked to and the system allows it
//(data() is then the whole file); else it reads a piece at a time. Reads past the end give zeros, as vfs::disk's do,
//and so does a read the system fails.
struct descriptor : file {
  static auto open(int given, bool map = true) -> std::shared_ptr<descriptor> {
    struct enable_make_shared : descriptor { using descriptor::descriptor; };
    int own = dup(given);
    if(own < 0) return {};
    struct stat status;
    if(fstat(own, &status) != 0 || !S_ISREG(status.st_mode)) {
      close(own);
      return {};
    }
    auto instance = std::make_shared<enable_make_shared>();
    instance->_descriptor = own;
    instance->_size = u64(status.st_size);
    if(map && instance->_size) {
      void* mapped = mmap(nullptr, instance->_size, PROT_READ, MAP_SHARED, own, 0);
      if(mapped != MAP_FAILED) instance->_data = (u8*)mapped;
    }
    return instance;
  }

  ~descriptor() {
    if(_data) munmap(_data, _size);
    if(_descriptor >= 0) close(_descriptor);
  }

  auto data() const -> const u8* override { return _data; }
  auto data() -> u8* override { return _data; }
  auto size() const -> u64 override { return _size; }
  auto offset() const -> u64 override { return _offset; }
  auto resize(u64 size) -> bool override { return false; }

  auto seek(s64 offset, index mode = index::absolute) -> void override {
    if(mode == index::absolute) _offset  = (u64)offset;
    if(mode == index::relative) _offset += (s64)offset;
  }

  auto read() -> u8 override {
    u8 byte = 0;
    read({&byte, 1});
    return byte;
  }

  auto read(std::span<u8> span) -> void override {
    u64 count = _offset < _size ? std::min<u64>(span.size(), _size - _offset) : 0;
    if(_data) {
      memcpy(span.data(), _data + _offset, count);
    } else {
      for(u64 done = 0; done < count;) {
        ssize_t got = pread(_descriptor, span.data() + done, count - done, off_t(_offset + done));
        if(got < 0 && errno == EINTR) continue;  //interrupted before it read anything: again
        if(got <= 0) { count = done; break; }
        done += u64(got);
      }
    }
    memset(span.data() + count, 0, span.size() - count);
    _offset += span.size();
  }

  auto write(u8 data) -> void override {}

private:
  descriptor() = default;
  descriptor(const descriptor&) = delete;
  auto operator=(const descriptor&) -> descriptor& = delete;

  int _descriptor = -1;
  u8* _data = nullptr;
  u64 _size = 0;
  u64 _offset = 0;
};

}
#endif
