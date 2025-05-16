// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/platform.h"

#include "installer/cgpt_manager.h"
#include "installer/inst_util.h"

Platform::~Platform() = default;

std::string PlatformImpl::DumpKernelConfig(
    const base::FilePath& kernel_dev) const {
  return ::DumpKernelConfig(kernel_dev);
}

std::optional<Guid> PlatformImpl::GetPartitionUniqueId(
    const base::FilePath& base_device, PartitionNum partition_num) const {
  CgptManager cgpt(base_device);
  Guid guid;
  if (cgpt.GetPartitionUniqueId(partition_num, &guid) !=
      CgptErrorCode::kSuccess) {
    return std::nullopt;
  }

  return guid;
}
