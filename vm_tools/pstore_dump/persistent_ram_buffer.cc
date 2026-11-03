// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/pstore_dump/persistent_ram_buffer.h"
#include "vm_tools/common/pstore.h"

#include <stdint.h>
#include <unistd.h>

#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/memory_mapped_file.h>
#include <base/logging.h>

namespace vm_tools::pstore_dump {

namespace {

// The values to compute offsets of the ring buffers in the same way to
// fs/pstore/ram.c.
constexpr int kDumpMemSize = kArcVmRamoopsSize - kArcVmRamoopsConsoleSize -
                             kArcVmRamoopsFtraceSize - kArcVmRamoopsPmsgSize;
constexpr int kZoneCount = kDumpMemSize / kArcVmRamoopsRecordSize;
constexpr int kZoneSize = kDumpMemSize / kZoneCount;

// FindPersistentRamBufferForConsoleOutput finds the ring buffer for kernel's
// console output from .pstore file.
//
// This function depends the internal implementation of the linux kernel about
// ramoops, and also assumes that the values of kernel parameters about ramoops.
const persistent_ram_buffer* FindPersistentRamBufferForConsoleOutput(
    const char* pstore, size_t pstore_size) {
  if (pstore_size != kArcVmRamoopsSize) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "expected file size is "
               << kArcVmRamoopsSize << " bytes but the actual size is "
               << pstore_size << " bytes.";
    return nullptr;
  }

  constexpr int offset = kZoneSize * kZoneCount;
  const persistent_ram_buffer* console =
      reinterpret_cast<const persistent_ram_buffer*>(pstore + offset);
  if (console->sig != PERSISTENT_RAM_SIG) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "ring buffer doesn't have the expected signature.";
    return nullptr;
  }
  return console;
}

// FindPersistentRamBufferForDmesg finds the |index|-th ring buffer for kernel's
// dmesg from .pstore file, which is a backend of dmesg-ramoops-|index|.
const persistent_ram_buffer* FindPersistentRamBufferForDmesg(const char* pstore,
                                                             size_t pstore_size,
                                                             int index) {
  if (pstore_size != kArcVmRamoopsSize) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "expected file size is "
               << kArcVmRamoopsSize << " bytes but the actual size is "
               << pstore_size << " bytes.";
    return nullptr;
  }

  // Computing the offset of the ring buffer in the same way to fs/pstore/ram.c.
  if (index < 0 || kZoneCount <= index) {
    LOG(ERROR) << "The given index (i = " << index
               << ") of the dmesg ring buffers is out of bounds (0 <= i < "
               << kZoneCount << ").";
    return nullptr;
  }

  const int offset = index * kZoneSize;
  const persistent_ram_buffer* console =
      reinterpret_cast<const persistent_ram_buffer*>(pstore + offset);
  if (console->sig != PERSISTENT_RAM_SIG) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "ring buffer doesn't have the expected signature.";
    return nullptr;
  }
  return console;
}

bool WritePersistentRamBufferToFd(const persistent_ram_buffer* buf,
                                  size_t buf_capacity,
                                  int fd) {
  std::string content;
  if (!GetPersistentRamBufferContent(buf, buf_capacity, &content)) {
    LOG(ERROR) << "Failed to read the content of persistent ram buffer";
    return false;
  }

  if (!base::WriteFileDescriptor(fd, content)) {
    PLOG(ERROR) << "Failed to write data to stdout";
    return false;
  }
  return true;
}

}  // namespace

// GetPersistentRamBufferContent read all logs from |buf| and write them to
// |out_content|.
//
// The |buf| is a memory mapped file which may be shared with ARCVM Linux
// kernel. We should load the entire logs at once to reduce synchronization
// issues.
bool GetPersistentRamBufferContent(const persistent_ram_buffer* buf,
                                   size_t buf_capacity,
                                   std::string* out_content) {
  DCHECK(out_content);

  const uint32_t next_write_offset = buf->start;
  const uint32_t size = buf->size;
  // Before wrap-around, `next_write_offset == size` (< `buf_capacity`).
  // After wrap-around, `size == buf_capacity` and `next_write_offset` wraps
  // within [0, `buf_capacity`).
  // Thus `next_write_offset <= size <= buf_capacity` always holds. Checking
  // `next_write_offset <= size` prevents reading past the valid `size` bytes
  // (or past `buf_capacity`) and prevents `buf_capacity - next_write_offset`
  // from underflowing.
  if (size > buf_capacity || next_write_offset > size) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "ring buffer header is out of bounds (start="
               << next_write_offset << ", size=" << size
               << ", capacity=" << buf_capacity << ").";
    return false;
  }

  out_content->clear();

  const char* data = reinterpret_cast<const char*>(buf->data);

  // Before wrap-around (size < buf_capacity, next_write_offset == size):
  //   0                 next_write_offset                    buf_capacity
  //   |========================|...................................|
  //   |        content         |              unused               |
  //
  // After wrap-around (size == buf_capacity):
  //   0                 next_write_offset                    buf_capacity
  //   |========================|===================================|
  //   |     newer content      |           older content           |
  if (size == buf_capacity) {
    out_content->append(data + next_write_offset,
                        buf_capacity - next_write_offset);
  }

  out_content->append(data, next_write_offset);
  return true;
}

bool HandlePstore(const base::FilePath& path) {
  std::string pstore;
  if (!base::ReadFileToString(path, &pstore)) {
    LOG(ERROR) << "Failed to read file: " << path;
    return false;
  }

  const persistent_ram_buffer* buf = FindPersistentRamBufferForConsoleOutput(
      reinterpret_cast<const char*>(pstore.c_str()), pstore.size());
  if (buf == nullptr) {
    LOG(ERROR) << "The persistent_ram_buffer for console is not found.";
    return false;
  }

  constexpr size_t kConsoleCapacity =
      kArcVmRamoopsConsoleSize - sizeof(persistent_ram_buffer);
  return WritePersistentRamBufferToFd(buf, kConsoleCapacity, STDOUT_FILENO);
}

bool HandlePstoreDmesg(const base::FilePath& path) {
  std::string pstore;
  if (!base::ReadFileToString(path, &pstore)) {
    LOG(ERROR) << "Failed to read file: " << path;
    return false;
  }

  constexpr size_t kDmesgCapacity = kZoneSize - sizeof(persistent_ram_buffer);
  for (int i = 0; i < kZoneCount; ++i) {
    const persistent_ram_buffer* buf = FindPersistentRamBufferForDmesg(
        reinterpret_cast<const char*>(pstore.c_str()), pstore.size(), i);
    if (buf == nullptr) {
      LOG(ERROR) << "The persistent_ram_buffer for dmesg is not found.";
      return false;
    }

    if (!WritePersistentRamBufferToFd(buf, kDmesgCapacity, STDOUT_FILENO)) {
      return false;
    }
  }
  return true;
}

}  // namespace vm_tools::pstore_dump
