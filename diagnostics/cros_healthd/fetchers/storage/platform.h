// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_PLATFORM_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_PLATFORM_H_

#include <cstdint>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/common/statusor.h"

namespace diagnostics {

// Platform wraps low-level enquiries to the system in order to be able to mock
// or fake those calls in tests.
class Platform {
 public:
  Platform() = default;
  Platform(const Platform&) = delete;
  Platform(Platform&&) = delete;
  Platform& operator=(const Platform&) = delete;
  Platform& operator=(Platform&&) = delete;
  virtual ~Platform() = default;

  // Returns physical device name underlying the root partition. The result is
  // only the node name, not the full path, and is assumed to lie in '/dev/'.
  virtual std::string GetRootDeviceName() const;
  // Returns size of the block device in bytes.
  virtual StatusOr<uint64_t> GetDeviceSizeBytes(
      const base::FilePath& dev_path) const;
  // Returns size of a block of the block device in bytes.
  virtual StatusOr<uint64_t> GetDeviceBlockSizeBytes(
      const base::FilePath& dev_path) const;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_PLATFORM_H_
