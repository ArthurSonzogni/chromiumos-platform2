// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "farfetchd/prefetch_helper.h"

#include <fcntl.h>

#include <cstdio>
#include <iomanip>
#include <string>

#include <base/files/file_path.h>
#include <base/posix/eintr_wrapper.h>

#include "libstorage/platform/platform.h"

#define READAHEAD_MAX_LENGTH (32 * 4096)

namespace farfetchd {

PrefetchHelper::PrefetchHelper(libstorage::Platform* platform) : p(platform) {}

// Preload file by reading it into memory.
// Log the elapsed time.
bool PrefetchHelper::PreloadFile(const base::FilePath& path) {
  const auto start = base::Time::Now();
  std::string buffer;
  struct stat st;

  FILE* file = p->OpenFile(path, "r");

  const int fd = fileno(file);

  if (!p->Stat(path, &st)) {
    LOG(ERROR) << "stat Failed on file: " << path;
    p->CloseFile(file);
    return false;
  }

  ssize_t bytes_read = 0;
  buffer.resize(st.st_size);

  while (true) {
    const ssize_t count = HANDLE_EINTR(p->PreadFile(
        fd, &buffer[bytes_read], buffer.size() - bytes_read, bytes_read));
    if (count < 0) {
      LOG(ERROR) << "pread Failed on file: " << path;
      p->CloseFile(file);
      return false;
    }

    bytes_read += count;

    if (bytes_read == buffer.size()) {
      const auto end = base::Time::Now();
      const auto diff = end - start;
      LOG(INFO) << "Time Elapsed (Preload): " << std::fixed
                << std::setprecision(2) << diff.InMillisecondsF() << " ms";

      p->CloseFile(file);
      return true;
    }
  }
}

// Preload file by reading it into memory asynchronously.
// Scheduling is handled by the kernel so the actual caching
// may be delayed.
// Elapsed time reflects the time it took to complete the syscalls.
// NOT the actual time to complete the caching.
bool PrefetchHelper::PreloadFileAsync(const base::FilePath& path) {
  const auto start = base::Time::Now();
  struct stat st;

  FILE* file = p->OpenFile(path, "r");

  const int fd = fileno(file);

  if (!p->Stat(path, &st)) {
    LOG(ERROR) << "stat Failed";
    p->CloseFile(file);
    return false;
  }

  ssize_t size = st.st_size;
  off_t offset = 0;

  while (size > 0) {
    const off_t read_length =
        (size <= READAHEAD_MAX_LENGTH) ? size : READAHEAD_MAX_LENGTH;

    int success = p->ReadaheadFile(fd, offset, read_length);
    if (success < 0) {
      LOG(ERROR) << "readahead Failed";
      p->CloseFile(file);
      return false;
    }

    offset += read_length;
    size -= read_length;
  }

  const auto end = base::Time::Now();
  const auto diff = end - start;
  LOG(INFO) << "Time Elapsed (PreloadAsync): " << std::fixed
            << std::setprecision(2) << diff.InMillisecondsF() << " ms";

  p->CloseFile(file);
  return true;
}

// Preload file by mmapping it into memory.
// Log the elapsed time.
// Mmap runs async and elapsed time only reflects the time for the syscall
// Not the time until the data is cached.
bool PrefetchHelper::PreloadFileMmap(const base::FilePath& path) {
  const auto start = base::Time::Now();
  struct stat st;

  FILE* file = p->OpenFile(path, "r");

  const int fd = fileno(file);

  if (!p->Stat(path, &st)) {
    LOG(ERROR) << "stat Failed";
    p->CloseFile(file);
    return false;
  }

  void* map;
  map = p->MmapFile(nullptr, st.st_size, PROT_READ,
                    MAP_FILE | MAP_POPULATE | MAP_SHARED, fd, 0);

  if (map == MAP_FAILED) {
    LOG(ERROR) << "mmap Failed";
    p->CloseFile(file);
    return false;
  }
  const auto end = base::Time::Now();

  if (munmap(map, st.st_size) == -1) {
    p->CloseFile(file);
    LOG(ERROR) << "munmap Failed";
    return false;
  }

  p->CloseFile(file);

  const auto diff = end - start;
  LOG(INFO) << "Time Elapsed (PreloadMmap): " << std::fixed
            << std::setprecision(2) << diff.InMillisecondsF() << " ms";

  return true;
}

}  // namespace farfetchd
