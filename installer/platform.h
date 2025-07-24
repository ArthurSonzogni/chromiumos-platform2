// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_PLATFORM_H_
#define INSTALLER_PLATFORM_H_

#include <optional>
#include <string>

#include <vboot/gpt.h>

#include "installer/inst_util.h"

namespace base {
class FilePath;
};

// DMI keys to values typically exposed via
// sysfs at /sys/class/dmi/id/*
enum class DmiKey {
  kSysVendor,
  kProductName,
};

// Abstract interface for accessing system services.
class Platform {
 public:
  virtual ~Platform();

  // Read the kernel config from a vboot kernel partition.
  virtual std::string DumpKernelConfig(
      const base::FilePath& kernel_dev) const = 0;

  // Get the unique partition GUID for partition `partition_num` on
  // device `base_device`.
  virtual std::optional<Guid> GetPartitionUniqueId(
      const base::FilePath& base_device, PartitionNum partition_num) const = 0;

  // Read a DMI value from the system.
  virtual std::optional<std::string> ReadDmi(DmiKey key) const = 0;
};

// Real implementation of Platform (used outside of tests).
class PlatformImpl : public Platform {
 public:
  std::string DumpKernelConfig(const base::FilePath& kernel_dev) const override;

  std::optional<Guid> GetPartitionUniqueId(
      const base::FilePath& base_device,
      PartitionNum partition_num) const override;

  std::optional<std::string> ReadDmi(DmiKey key) const override;
};

#endif  // INSTALLER_PLATFORM_H_
