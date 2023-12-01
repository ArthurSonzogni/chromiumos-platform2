// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_UTILS_H_
#define SWAP_MANAGEMENT_UTILS_H_

#include <string>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/file_path.h>
#include <base/scoped_generic.h>
#include <base/process/process_metrics.h>
#include <sys/statfs.h>

namespace swap_management {
constexpr char kZramDeviceFile[] = "/dev/zram0";
constexpr char kZramSysfsDir[] = "/sys/block/zram0";
constexpr uint32_t kMiB = 1048576;
constexpr uint32_t kPageSize = 4096;

template <typename T>
T** GetSingleton() {
  static T* inst = new T;
  return &inst;
}

class Utils {
 public:
  static Utils* Get();
  static void OverrideForTesting(Utils* util);

  // Virtual for testing
  virtual absl::Status RunProcessHelper(
      const std::vector<std::string>& commands);
  virtual absl::Status RunProcessHelper(
      const std::vector<std::string>& commands, std::string* output);
  virtual absl::Status WriteFile(const base::FilePath& path,
                                 const std::string& data);
  virtual absl::Status ReadFileToStringWithMaxSize(const base::FilePath& path,
                                                   std::string* contents,
                                                   size_t max_size);
  virtual absl::Status ReadFileToString(const base::FilePath& path,
                                        std::string* contents);
  virtual absl::Status DeleteFile(const base::FilePath& path);
  virtual absl::Status PathExists(const base::FilePath& path);
  virtual absl::Status Fallocate(const base::FilePath& path, size_t size);
  virtual absl::Status CreateDirectory(const base::FilePath& path);
  virtual absl::Status SetPosixFilePermissions(const base::FilePath& path,
                                               int mode);
  virtual absl::Status Mount(const std::string& source,
                             const std::string& target,
                             const std::string& fs_type,
                             uint64_t mount_flags,
                             const std::string& data);
  virtual absl::Status Umount(const std::string& target);
  virtual absl::StatusOr<struct statfs> GetStatfs(const std::string& path);
  virtual absl::StatusOr<std::string> GenerateRandHex(size_t size);
  virtual absl::StatusOr<base::SystemMemoryInfoKB> GetSystemMemoryInfo();

  uint64_t RoundupMultiple(uint64_t number, uint64_t alignment);
  absl::StatusOr<bool> SimpleAtob(const std::string& str);
  template <typename int_type>
  absl::StatusOr<int_type> SimpleAtoi(const std::string& str) {
    int_type output;
    if (!absl::SimpleAtoi<int_type>(str, &output))
      return absl::InvalidArgumentError("Failed to convert " + str +
                                        " to an integer value.");

    return output;
  }

 private:
  Utils() = default;
  Utils& operator=(const Utils&) = delete;
  Utils(const Utils&) = delete;

  virtual ~Utils() = default;

  friend class MockUtils;
  friend Utils** GetSingleton<Utils>();
};

struct ScopedFilePathTraits {
  static const base::FilePath InvalidValue();
  static void Free(const base::FilePath path);
};

// Delete the FilePath when the object is destroyed.
using ScopedFilePath =
    base::ScopedGeneric<base::FilePath, ScopedFilePathTraits>;

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_UTILS_H_
