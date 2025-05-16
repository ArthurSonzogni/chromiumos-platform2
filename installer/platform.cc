// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/platform.h"

#include "installer/inst_util.h"

Platform::~Platform() = default;

std::string PlatformImpl::DumpKernelConfig(
    const base::FilePath& kernel_dev) const {
  return ::DumpKernelConfig(kernel_dev);
}
