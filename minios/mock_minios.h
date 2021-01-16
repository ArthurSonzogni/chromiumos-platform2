// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_MINIOS_H_
#define MINIOS_MOCK_MINIOS_H_

#include <gmock/gmock.h>

#include "minios/minios.h"

namespace minios {

class MockMiniOs : public MiniOsInterface {
 public:
  MockMiniOs() = default;

  MOCK_METHOD(bool,
              GetState,
              (State * state_out, brillo::ErrorPtr* err),
              (override));

 private:
  MockMiniOs(const MockMiniOs&) = delete;
  MockMiniOs& operator=(const MockMiniOs&) = delete;
};

}  // namespace minios

#endif  // MINIOS_MOCK_MINIOS_H_
