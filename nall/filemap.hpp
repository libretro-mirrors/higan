#pragma once

#include <nall/file.hpp>
#include <nall/stdint.hpp>
#include <nall/windows/utf8.hpp>

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

namespace nall {

struct filemap {
  enum class mode : unsigned { read, write, readwrite, writeread };

  filemap() { p_ctor(); }
  filemap(const string& filename, mode mode_) { p_ctor(); p_open(filename, mode_); }
  ~filemap() { p_dtor(); }

  explicit operator bool() const { return open(); }
  auto open() const -> bool { return p_open(); }
  auto open(const string& filename, mode mode_) -> bool { return p_open(filename, mode_); }
  auto close() -> void { return p_close(); }
  auto size() const -> unsigned { return p_size; }
  auto data() -> uint8_t* { return p_handle; }
  auto data() const -> const uint8_t* { return p_handle; }

private:
  uint8_t* p_handle = nullptr;
  unsigned p_size = 0;

  #if defined(API_WINDOWS)
  //=============
  //MapViewOfFile
  //=============

  HANDLE p_filehandle;
  HANDLE p_maphandle;

  auto p_open() const -> bool {
    return p_handle;
  }

  auto p_open(const string& filename, mode mode_) -> bool {
    if(file::exists(filename) && file::size(filename) == 0) {
      p_handle = nullptr;
      p_size = 0;
      return true;
    }

    int desired_access, creation_disposition, flprotect, map_access;

    switch(mode_) {
    default: return false;
    case mode::read:
      desired_access = GENERIC_READ;
      creation_disposition = OPEN_EXISTING;
      flprotect = PAGE_READONLY;
      map_access = FILE_MAP_READ;
      break;
    case mode::write:
      //write access requires read access
      desired_access = GENERIC_WRITE;
      creation_disposition = CREATE_ALWAYS;
      flprotect = PAGE_READWRITE;
      map_access = FILE_MAP_ALL_ACCESS;
      break;
    case mode::readwrite:
      desired_access = GENERIC_READ | GENERIC_WRITE;
      creation_disposition = OPEN_EXISTING;
      flprotect = PAGE_READWRITE;
      map_access = FILE_MAP_ALL_ACCESS;
      break;
    case mode::writeread:
      desired_access = GENERIC_READ | GENERIC_WRITE;
      creation_disposition = CREATE_NEW;
      flprotect = PAGE_READWRITE;
      map_access = FILE_MAP_ALL_ACCESS;
      break;
    }

    p_filehandle = CreateFileW(utf16_t(filename), desired_access, FILE_SHARE_READ, nullptr,
      creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(p_filehandle == INVALID_HANDLE_VALUE) return false;

    p_size = GetFileSize(p_filehandle, nullptr);

    p_maphandle = CreateFileMapping(p_filehandle, nullptr, flprotect, 0, p_size, nullptr);
    if(p_maphandle == INVALID_HANDLE_VALUE) {
      CloseHandle(p_filehandle);
      p_filehandle = INVALID_HANDLE_VALUE;
      return false;
    }

    p_handle = (uint8_t*)MapViewOfFile(p_maphandle, map_access, 0, 0, p_size);
    return p_handle;
  }

  auto p_close() -> void {
    if(p_handle) {
      UnmapViewOfFile(p_handle);
      p_handle = nullptr;
    }

    if(p_maphandle != INVALID_HANDLE_VALUE) {
      CloseHandle(p_maphandle);
      p_maphandle = INVALID_HANDLE_VALUE;
    }

    if(p_filehandle != INVALID_HANDLE_VALUE) {
      CloseHandle(p_filehandle);
      p_filehandle = INVALID_HANDLE_VALUE;
    }
  }

  auto p_ctor() -> void {
    p_filehandle = INVALID_HANDLE_VALUE;
    p_maphandle  = INVALID_HANDLE_VALUE;
  }

  auto p_dtor() -> void {
    close();
  }

  #else
  //====
  //mmap
  //====

  int p_fd;

  auto p_open() const -> bool {
    return p_handle;
  }

  auto p_open(const string& filename, mode mode_) -> bool {
    if(file::exists(filename) && file::size(filename) == 0) {
      p_handle = nullptr;
      p_size = 0;
      return true;
    }

    int open_flags, mmap_flags;

    switch(mode_) {
    default: return false;
    case mode::read:
      open_flags = O_RDONLY;
      mmap_flags = PROT_READ;
      break;
    case mode::write:
      open_flags = O_RDWR | O_CREAT;  //mmap() requires read access
      mmap_flags = PROT_WRITE;
      break;
    case mode::readwrite:
      open_flags = O_RDWR;
      mmap_flags = PROT_READ | PROT_WRITE;
      break;
    case mode::writeread:
      open_flags = O_RDWR | O_CREAT;
      mmap_flags = PROT_READ | PROT_WRITE;
      break;
    }

    p_fd = ::open(filename, open_flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if(p_fd < 0) return false;

    struct stat p_stat;
    fstat(p_fd, &p_stat);
    p_size = p_stat.st_size;

    p_handle = (uint8_t*)mmap(nullptr, p_size, mmap_flags, MAP_SHARED, p_fd, 0);
    if(p_handle == MAP_FAILED) {
      p_handle = nullptr;
      ::close(p_fd);
      p_fd = -1;
      return false;
    }

    return p_handle;
  }

  auto p_close() -> void {
    if(p_handle) {
      munmap(p_handle, p_size);
      p_handle = nullptr;
    }

    if(p_fd >= 0) {
      ::close(p_fd);
      p_fd = -1;
    }
  }

  auto p_ctor() -> void {
    p_fd = -1;
  }

  auto p_dtor() -> void {
    p_close();
  }

  #endif
};

}
