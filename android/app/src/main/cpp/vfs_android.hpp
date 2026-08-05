#pragma once

#include <nall/vfs/vfs.hpp>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace nall::vfs {

struct android : file {
  ~android() { close(); }

  static auto open(int fd) -> std::shared_ptr<android> {
    struct enable_make_shared : android { using android::android; };
    auto instance = std::make_shared<enable_make_shared>();
    if(!instance->_open(fd)) return {};
    return instance;
  }

  auto readable() const -> bool override { return true; }
  auto writable() const -> bool override { return false; }
  auto data() const -> const u8* override { return _data; }
  auto data() -> u8* override { return _data; }
  auto size() const -> u64 override { return _size; }
  auto offset() const -> u64 override { return _offset; }

  auto resize(u64 size) -> bool override { return false; }

  auto seek(s64 offset, index mode = index::absolute) -> void override {
    if(mode == index::absolute) _offset  = (u64)offset;
    if(mode == index::relative) _offset += (s64)offset;
    if(_offset > _size) _offset = _size;
  }

  auto read() -> u8 override {
    if(_offset >= _size) return 0x00;
    return _data[_offset++];
  }

  auto read(std::span<u8> span) -> void override {
    if (_offset >= _size) {
      memset(span.data(), 0, span.size());
      return;
    }
    u64 available = _size - _offset;
    u64 toRead = std::min((u64)span.size(), available);
    memcpy(span.data(), _data + _offset, toRead);
    if (toRead < span.size()) {
      memset(span.data() + toRead, 0, span.size() - toRead);
    }
    _offset += toRead;
  }

  auto write(u8 data) -> void override {}

  auto close() -> void {
    if(_data) munmap(_data, _size);
    if(_fd != -1) ::close(_fd);
    _data = nullptr;
    _fd = -1;
    _size = 0;
    _offset = 0;
  }

private:
  android() = default;
  auto _open(int fd) -> bool {
    _fd = fd;
    struct stat st;
    if(fstat(_fd, &st) != 0) return false;
    _size = st.st_size;
    if(_size == 0) return true;
    _data = (u8*)mmap(nullptr, _size, PROT_READ, MAP_SHARED, _fd, 0);
    if(_data == MAP_FAILED) {
      _data = nullptr;
      return false;
    }
    return true;
  }

  int _fd = -1;
  u8* _data = nullptr;
  u64 _size = 0;
  u64 _offset = 0;
};

}
