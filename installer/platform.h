// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_PLATFORM_H_
#define INSTALLER_PLATFORM_H_

#include <string>

namespace base {
class FilePath;
};

// Abstract interface for accessing system services.
class Platform {
 public:
  virtual ~Platform();

  // Read the kernel config from a vboot kernel partition.
  virtual std::string DumpKernelConfig(const base::FilePath& kernel_dev) = 0;
};

// Real implementation of Platform (used outside of tests).
class PlatformImpl : public Platform {
 public:
  std::string DumpKernelConfig(const base::FilePath& kernel_dev) override;
};

#endif  // INSTALLER_PLATFORM_H_
