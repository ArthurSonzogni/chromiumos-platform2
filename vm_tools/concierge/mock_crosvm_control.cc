// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mock_crosvm_control.h"

#include <memory>

namespace vm_tools::concierge {

void MockCrosvmControl::Init() {
  CrosvmControl::SetInstance(
      std::unique_ptr<CrosvmControl>(new MockCrosvmControl()));
}

MockCrosvmControl* MockCrosvmControl::Get() {
  return reinterpret_cast<MockCrosvmControl*>(CrosvmControl::Get());
}

}  // namespace vm_tools::concierge
