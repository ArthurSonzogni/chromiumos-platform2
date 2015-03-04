// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_INST_UTIL_H_
#define INSTALLER_INST_UTIL_H_

#include <string>
#include <vector>

// A class to automatically close a pure file descriptor. A ScopedFileDescriptor
// object can be treated as any regular file descriptor. You can call read(),
// write(), etc. on these objects.
// This is NOT the same as update_engine::ScopedFileDescriptorCloser because
// that other class wraps an update_engine::FileDescriptor.
class ScopedFileDescriptor {
 public:
  explicit ScopedFileDescriptor(int fd) : fd_(fd) {}
  virtual ~ScopedFileDescriptor();

  // Release the file descriptor, no longer manage it.
  int release();
  // Release and close the file descriptor.
  int close();
  operator int() { return fd_; }

 private:
  int fd_;

  ScopedFileDescriptor(const ScopedFileDescriptor& other) {}
  void operator =(const ScopedFileDescriptor& other) {}
};

// A class to automatically remove directories/files with nftw().
// The removal is done at object destruction time and hence no error will be
// boubled up. If need to, use release() and handle the deletion yourself.
class ScopedPathRemover {
 public:
  explicit ScopedPathRemover(const std::string& root) : root_(root) {}
  virtual ~ScopedPathRemover();

  // Return the root path and no longer remove it.
  std::string release();

 private:
  std::string root_;

  ScopedPathRemover(const ScopedPathRemover& other) {}
  void operator=(const ScopedPathRemover& other) {}
};

#define RUN_OR_RETURN_FALSE(_x)                                 \
  do {                                                          \
    if (RunCommand(_x) != 0) return false;                      \
  } while (0)

// Find a pointer to the first element of a statically sized array.
template<typename T, size_t N>
T * begin(T (&ra)[N]) {
    return ra + 0;
}

// Find a pointer to the element after the end of a statically sized array.
template<typename T, size_t N>
T * end(T (&ra)[N]) {
    return ra + N;
}

// Return true if |s| starts with |prefix|.
bool StartsWith(const std::string& s, const std::string& prefix);

// Return true if |s| ends with |suffix|.
bool EndsWith(const std::string& s, const std::string& suffix);

// Start a timer (there can only be one active).
void LoggingTimerStart();

// Log how long since LoggingTimerStart was last called.
void LoggingTimerFinish();

__attribute__((format(printf, 1, 2)))
std::string StringPrintf(const char* format, ...);

void SplitString(const std::string& str,
                 char split,
                 std::vector<std::string>* output);

void JoinStrings(const std::vector<std::string>& strs,
                const std::string& split,
                std::string* output);

// This is a place holder to invoke the backing scripts. Once all scripts have
// been rewritten as library calls this command should be deleted.
int RunCommand(const std::string& command);

bool ReadFileToString(const std::string& path, std::string* contents);

bool WriteStringToFile(const std::string& contents, const std::string& path);

// Copies a single file.
bool CopyFile(const std::string& from_path, const std::string& to_path);

bool LsbReleaseValue(const std::string& file,
                     const std::string& key,
                     std::string* result);

bool VersionLess(const std::string& left,
                 const std::string& right);

// Given root partition dev node (such as /dev/sda3, /dev/mmcblk0p3,
// /dev/ubiblock3_0), return the block dev (/dev/sda, /dev/mmcblk0, /dev/mtd0).
std::string GetBlockDevFromPartitionDev(const std::string& partition_dev);

// Given root partition dev node (such as /dev/sda3, /dev/mmcblk0p3,
// /dev/ubiblock3_0), return the partition number (3).
int GetPartitionFromPartitionDev(const std::string& partition_dev);

// Given block dev node (/dev/sda, /dev/mmcblk0, /dev/mtd0) and a partition
// number (such as 3), return a new dev node pointing to the partition
// (/dev/sda3, /dev/mmcblk0p3, /dev/ubiblock3_0). On NAND media, the partitions
// can change widely, though they have the same block /dev/mtd0:
//   * Root partitions ubiblockX_0
//   * Kernel partitions mtdX
//   * Stateful and OEM partitions ubiX_0
std::string MakePartitionDev(const std::string& partition_dev,
                             int partition);

// Convert /blah/file to /blah
std::string Dirname(const std::string& filename);

// rm *pack from /dirname
bool RemovePackFiles(const std::string& dirname);

// Create an empty file
bool Touch(const std::string& filename);

// Replace the first instance of pattern in the file with value.
bool ReplaceInFile(const std::string& pattern,
                   const std::string& value,
                   const std::string& path);

// Replace all instances of pattern in target with value
void ReplaceAll(std::string* target,
                const std::string& pattern,
                const std::string& value);

// See bug chromium-os:11517. This fixes an old FS corruption problem.
bool R10FileSystemPatch(const std::string& dev_name);

// Mark ext2 (3 or 4???) filesystem RW
bool MakeFileSystemRw(const std::string& dev_name, bool rw);

// hdparm -r 1 /device
bool MakeDeviceReadOnly(const std::string& dev_name);

// Conveniently invoke the external dump_kernel_config library
std::string DumpKernelConfig(const std::string& kernel_dev);

// ExtractKernelNamedArg(DumpKernelConfig(..), "root") -> /dev/dm-0
// This understands quoted values. dm -> "a b c, foo=far" (strips quotes)
std::string ExtractKernelArg(const std::string& kernel_config,
                             const std::string& tag);

// Take a kernel style argument list and modify a single argument
// value. Quotes will be added to the value if needed.
bool SetKernelArg(const std::string& tag,
                  const std::string& value,
                  std::string* kernel_config);

// IsReadonly determines if the name devices should be treated as
// read-only. This is based on the device name being prefixed with
// "/dev/dm". This catches both cases where verity may be /dev/dm-0
// or /dev/dm-1.
bool IsReadonly(const std::string& device);

#endif  // INSTALLER_INST_UTIL_H_
