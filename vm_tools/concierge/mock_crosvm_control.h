// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef VM_TOOLS_CONCIERGE_MOCK_CROSVM_CONTROL_H_
#define VM_TOOLS_CONCIERGE_MOCK_CROSVM_CONTROL_H_

#include "vm_tools/concierge/crosvm_control.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace vm_tools::concierge {

class MockCrosvmControl : public CrosvmControl {
 public:
  static void Init();
  static MockCrosvmControl* Get();

  MOCK_METHOD(bool, StopVm, (const char*));
  MOCK_METHOD(bool, SuspendVm, (const char*));
  MOCK_METHOD(bool, ResumeVm, (const char*));
  MOCK_METHOD(bool, MakeRtVm, (const char*));
  MOCK_METHOD(bool, SetBalloonSize, ((const char*), (size_t)));
  MOCK_METHOD(size_t, MaxUsbDevices, ());
  MOCK_METHOD(ssize_t,
              UsbList,
              ((const char*), (struct UsbDeviceEntry*), ssize_t));
  MOCK_METHOD(bool,
              UsbAttach,
              ((const char*),
               uint8_t,
               uint8_t,
               uint16_t,
               uint16_t,
               (const char*),
               (uint8_t*)));
  MOCK_METHOD(bool, UsbDetach, ((const char*), uint8_t));
  MOCK_METHOD(bool,
              ModifyBattery,
              ((const char*), (const char*), (const char*), (const char*)));
  MOCK_METHOD(bool, ResizeDisk, ((const char*), size_t, uint64_t));
  MOCK_METHOD(bool,
              BalloonStats,
              ((const char*), (struct BalloonStatsFfi*), (uint64_t*)));
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_MOCK_CROSVM_CONTROL_H_
