// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_INST_UTIL_H_
#define INSTALLER_INST_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

extern const char kEnvIsInstall[];
extern const char kEnvIsFactoryInstall[];
extern const char kEnvIsRecoveryInstall[];

enum partition_nums_t {
  PART_NUM_KERN_A = 2,
  PART_NUM_ROOT_A = 3,
  PART_NUM_KERN_B = 4,
  PART_NUM_ROOT_B = 5,
  PART_NUM_KERN_C = 6,
  PART_NUM_ROOT_C = 7,
  PART_NUM_OEM = 8,
  PART_NUM_RWFW = 11,
  PART_NUM_EFI_SYSTEM = 12
};

// A class to automatically remove directories/files with nftw().
// The removal is done at object destruction time and hence no error will be
// boubled up. If need to, use release() and handle the deletion yourself.
class ScopedPathRemover {
 public:
  explicit ScopedPathRemover(const base::FilePath& root) : root_(root) {}
  virtual ~ScopedPathRemover();

  ScopedPathRemover(const ScopedPathRemover& other) = delete;
  void operator=(const ScopedPathRemover& other) = delete;

  // Return the root path and no longer remove it.
  base::FilePath Release();

 private:
  base::FilePath root_;
};

// Find a pointer to the first element of a statically sized array.
template <typename T, size_t N>
T* begin(T (&ra)[N]) {
  return ra + 0;
}

// Find a pointer to the element after the end of a statically sized array.
template <typename T, size_t N>
T* end(T (&ra)[N]) {
  return ra + N;
}

// Start a timer (there can only be one active).
void LoggingTimerStart();

// Log how long since LoggingTimerStart was last called.
void LoggingTimerFinish();

// This is a place holder to invoke the backing scripts. Once all scripts have
// been rewritten as library calls this command should be deleted.
int RunCommand(const std::vector<std::string>& cmdline);

// Write |content| to |fd| fully. This function will call write() as many times
// as needed to ensure that |content| is fully written. Return false on error.
bool WriteFullyToFileDescriptor(const std::string& content, int fd);

bool LsbReleaseValue(const base::FilePath& file,
                     const std::string& key,
                     std::string* result);

// Given root partition dev node (such as /dev/sda3, /dev/mmcblk0p3,
// /dev/ubiblock3_0), return the block dev (/dev/sda, /dev/mmcblk0, /dev/mtd0).
base::FilePath GetBlockDevFromPartitionDev(const base::FilePath& partition_dev);

// Given root partition dev node (such as /dev/sda3, /dev/mmcblk0p3,
// /dev/ubiblock3_0), return the partition number (3).
int GetPartitionFromPartitionDev(const base::FilePath& partition_dev);

// Given block dev node (/dev/sda, /dev/mmcblk0, /dev/mtd0) and a partition
// number (such as 3), return a new dev node pointing to the partition
// (/dev/sda3, /dev/mmcblk0p3, /dev/ubiblock3_0). On NAND media, the partitions
// can change widely, though they have the same block /dev/mtd0:
//   * Root partitions ubiblockX_0
//   * Kernel partitions mtdX
//   * Stateful and OEM partitions ubiX_0
base::FilePath MakePartitionDev(const base::FilePath& partition_dev,
                                int partition);

// rm *pack from /dirname
bool RemovePackFiles(const base::FilePath& dirname);

// Create an empty file
bool Touch(const base::FilePath& filename);

// Replace the first instance of pattern in the file with value.
bool ReplaceInFile(const std::string& pattern,
                   const std::string& value,
                   const base::FilePath& path);

// Replace all instances of pattern in target with value
void ReplaceAll(std::string* target,
                const std::string& pattern,
                const std::string& value);

// Mark ext2 (3 or 4???) filesystem RW
bool MakeFileSystemRw(const std::string& dev_name);

// Conveniently invoke the external dump_kernel_config library
std::string DumpKernelConfig(const base::FilePath& kernel_dev);

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
bool IsReadonly(const base::FilePath& device);

// Sets |result| with the current running kernel information like name, version,
// etc.
bool GetKernelInfo(std::string* result);

#endif  // INSTALLER_INST_UTIL_H_
