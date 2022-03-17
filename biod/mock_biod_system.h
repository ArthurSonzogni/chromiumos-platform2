// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_BIOD_SYSTEM_H_
#define BIOD_MOCK_BIOD_SYSTEM_H_

#include <gmock/gmock.h>

#include "biod/biod_system.h"

namespace biod {

class MockBiodSystem : public BiodSystem {
 public:
  MOCK_METHOD(bool, HardwareWriteProtectIsEnabled, (), (const, override));
};

}  // namespace biod

#endif  // BIOD_MOCK_BIOD_SYSTEM_H_
