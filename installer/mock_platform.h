// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INSTALLER_MOCK_PLATFORM_H_
#define INSTALLER_MOCK_PLATFORM_H_

#include <string>

#include <gmock/gmock.h>

#include "installer/platform.h"

class MockPlatform : public Platform {
 public:
  MOCK_METHOD(std::string,
              DumpKernelConfig,
              (const base::FilePath& kernel_dev),
              (const override));
};

#endif  // INSTALLER_MOCK_PLATFORM_H_
