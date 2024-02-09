// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_HARDWARE_H_
#define UPDATE_ENGINE_COMMON_MOCK_HARDWARE_H_

#include <string>

#include "update_engine/common/fake_hardware.h"

#include <gmock/gmock.h>

namespace chromeos_update_engine {

// A mocked, fake implementation of HardwareInterface.
class MockHardware : public HardwareInterface {
 public:
  MockHardware() {
    // Delegate all calls to the fake instance
    ON_CALL(*this, IsOfficialBuild())
        .WillByDefault(testing::Invoke(&fake_, &FakeHardware::IsOfficialBuild));
    ON_CALL(*this, IsNormalBootMode())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::IsNormalBootMode));
    ON_CALL(*this, IsRunningFromMiniOs())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::IsRunningFromMiniOs));
    ON_CALL(*this, AreDevFeaturesEnabled())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::AreDevFeaturesEnabled));
    ON_CALL(*this, IsOOBEEnabled())
        .WillByDefault(testing::Invoke(&fake_, &FakeHardware::IsOOBEEnabled));
    ON_CALL(*this, IsOOBEComplete(testing::_))
        .WillByDefault(testing::Invoke(&fake_, &FakeHardware::IsOOBEComplete));
    ON_CALL(*this, GetHardwareClass())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetHardwareClass));
    ON_CALL(*this, GetMinKernelKeyVersion())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetMinKernelKeyVersion));
    ON_CALL(*this, GetMinFirmwareKeyVersion())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetMinFirmwareKeyVersion));
    ON_CALL(*this, GetMaxFirmwareKeyRollforward())
        .WillByDefault(testing::Invoke(
            &fake_, &FakeHardware::GetMaxFirmwareKeyRollforward));
    ON_CALL(*this, SetMaxFirmwareKeyRollforward())
        .WillByDefault(testing::Invoke(
            &fake_, &FakeHardware::SetMaxFirmwareKeyRollforward));
    ON_CALL(*this, SetMaxKernelKeyRollforward())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::SetMaxKernelKeyRollforward));
    ON_CALL(*this, GetPowerwashCount())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetPowerwashCount));
    ON_CALL(*this, GetNonVolatileDirectory(testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetNonVolatileDirectory));
    ON_CALL(*this, GetPowerwashSafeDirectory(testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeHardware::GetPowerwashSafeDirectory));
    ON_CALL(*this, GetFirstActiveOmahaPingSent())
        .WillByDefault(testing::Invoke(
            &fake_, &FakeHardware::GetFirstActiveOmahaPingSent()));
    ON_CALL(*this, SetFirstActiveOmahaPingSent())
        .WillByDefault(testing::Invoke(
            &fake_, &FakeHardware::SetFirstActiveOmahaPingSent()));
  }
  MockHardware(const MockHardware&) = delete;
  MockHardware& operator=(const MockHardware&) = delete;

  ~MockHardware() override = default;

  // Hardware overrides.
  MOCK_CONST_METHOD0(IsOfficialBuild, bool());
  MOCK_CONST_METHOD0(IsNormalBootMode, bool());
  MOCK_CONST_METHOD0(IsRunningFromMiniOs, bool());
  MOCK_CONST_METHOD0(IsOOBEEnabled, bool());
  MOCK_CONST_METHOD1(IsOOBEComplete, bool(base::Time* out_time_of_oobe));
  MOCK_CONST_METHOD0(GetHardwareClass, std::string());
  MOCK_CONST_METHOD0(GetMinKernelKeyVersion, int());
  MOCK_CONST_METHOD0(GetMinFirmwareKeyVersion, int());
  MOCK_CONST_METHOD0(GetMaxFirmwareKeyRollforward, int());
  MOCK_CONST_METHOD1(SetMaxFirmwareKeyRollforward,
                     bool(int firmware_max_rollforward));
  MOCK_CONST_METHOD1(SetMaxKernelKeyRollforward,
                     bool(int kernel_max_rollforward));
  MOCK_CONST_METHOD0(GetPowerwashCount, int());
  MOCK_CONST_METHOD1(GetNonVolatileDirectory, bool(base::FilePath*));
  MOCK_CONST_METHOD1(GetPowerwashSafeDirectory, bool(base::FilePath*));
  MOCK_CONST_METHOD0(GetFirstActiveOmahaPingSent, bool());

  // Returns a reference to the underlying FakeHardware.
  FakeHardware& fake() { return fake_; }

 private:
  // The underlying FakeHardware.
  FakeHardware fake_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_HARDWARE_H_
