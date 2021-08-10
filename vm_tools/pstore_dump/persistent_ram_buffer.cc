// Copyright 2020 The Chromium OS Authors. All rights reserved.
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

namespace vm_tools {
namespace pstore_dump {

namespace {

// kernel parameters for ARCVM kernel.
// These values are decided by vm_concierge process, crosvm process, and Linux
// kernel. So it's difficult to avoid embedding them as constants. We can see
// some of these values from /proc/cmdline in ARCVM, but this file is
// unavailable when ARCVM is not running.
constexpr int kRamoopsMemSize = kArcVmPstoreSize;  // decided by vm_concierge
constexpr int kRamoopsRecordSize =
    kArcVmPstoreSize / 4;  // calculated at crosvm
constexpr int kRamoopsConsoleSize =
    kArcVmPstoreSize / 4;  // calculated at crosvm
constexpr int kRamoopsFtraceSize =
    0x1000;  // default for kernel module parameter ramoops.ftrace_size
constexpr int kRamoopsPmsgSize =
    0x1000;  // default for kernel module parameter ramoops.pmsg_size

// The values to compute offsets of the ring buffers in the same way to
// fs/pstore/ram.c.
constexpr int kDumpMemSize = kRamoopsMemSize - kRamoopsConsoleSize -
                             kRamoopsFtraceSize - kRamoopsPmsgSize;
constexpr int kZoneCount = kDumpMemSize / kRamoopsRecordSize;
constexpr int kZoneSize = kDumpMemSize / kZoneCount;

// FindPersistentRamBufferForConsoleOutput finds the ring buffer for kernel's
// console output from .pstore file.
//
// This function depends the internal implementation of the linux kernel about
// ramoops, and also assumes that the values of kernel parameters about ramoops.
const persistent_ram_buffer* FindPersistentRamBufferForConsoleOutput(
    const char* pstore, size_t pstore_size) {
  if (pstore_size != kRamoopsMemSize) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "expected file size is "
               << kRamoopsMemSize << " bytes but the actual size is "
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
  if (pstore_size != kRamoopsMemSize) {
    LOG(ERROR) << "The pstore file doesn't follow the expected format. The "
                  "expected file size is "
               << kRamoopsMemSize << " bytes but the actual size is "
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

bool WritePersistentRamBufferToFd(const persistent_ram_buffer* buf, int fd) {
  std::string content;
  if (!GetPersistentRamBufferContent(buf, buf->size, &content)) {
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

  out_content->clear();

  // buf->size matches with the capacity after the ring buffer has wrapped
  // around.
  if (buf->size == buf_capacity) {
    out_content->append(reinterpret_cast<const char*>(buf->data), buf->start,
                        buf_capacity - buf->start);
  }

  out_content->append(reinterpret_cast<const char*>(buf->data), 0, buf->start);
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

  return WritePersistentRamBufferToFd(buf, STDOUT_FILENO);
}

bool HandlePstoreDmesg(const base::FilePath& path) {
  std::string pstore;
  if (!base::ReadFileToString(path, &pstore)) {
    LOG(ERROR) << "Failed to read file: " << path;
    return false;
  }

  for (int i = 0; i < kZoneCount; ++i) {
    const persistent_ram_buffer* buf = FindPersistentRamBufferForDmesg(
        reinterpret_cast<const char*>(pstore.c_str()), pstore.size(), i);
    if (buf == nullptr) {
      LOG(ERROR) << "The persistent_ram_buffer for dmesg is not found.";
      return false;
    }

    if (!WritePersistentRamBufferToFd(buf, STDOUT_FILENO)) {
      return false;
    }
  }
  return true;
}

}  // namespace pstore_dump
}  // namespace vm_tools
