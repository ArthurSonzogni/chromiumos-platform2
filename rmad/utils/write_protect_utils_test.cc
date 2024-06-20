// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/write_protect_utils_impl.h"

#include <memory>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_ec_utils.h"
#include "rmad/utils/mock_futility_utils.h"
#include "rmad/utils/mock_gsc_utils.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class WriteProtectUtilsTest : public testing::Test {
 public:
  WriteProtectUtilsTest() = default;
  ~WriteProtectUtilsTest() override = default;

  struct UtilsArgs {
    bool hwwp_success = true;
    bool hwwp_enabled = true;
    bool apwp_success = true;
    bool apwp_enabled = true;
    bool ecwp_success = true;
    bool ecwp_enabled = true;
    bool chassis_open = false;
  };
  std::unique_ptr<WriteProtectUtilsImpl> CreateWriteProtectUtils(
      const UtilsArgs& args) {
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(args.hwwp_enabled),
                             Return(args.hwwp_success)));

    // Mock |EcUtils|.
    auto mock_ec_utils = std::make_unique<NiceMock<MockEcUtils>>();
    // Use |ecwp_success| to control the return value of enabling EC SWWP.
    ON_CALL(*mock_ec_utils, EnableEcSoftwareWriteProtection())
        .WillByDefault(Return(args.ecwp_success));
    ON_CALL(*mock_ec_utils, DisableEcSoftwareWriteProtection())
        .WillByDefault(Return(true));
    if (args.ecwp_success) {
      ON_CALL(*mock_ec_utils, GetEcWriteProtectionStatus())
          .WillByDefault(Return(args.ecwp_enabled));
    } else {
      ON_CALL(*mock_ec_utils, GetEcWriteProtectionStatus())
          .WillByDefault(Return(std::nullopt));
    }

    // Mock |FutilityUtils|.
    auto mock_futility_utils = std::make_unique<NiceMock<MockFutilityUtils>>();
    if (args.apwp_success) {
      ON_CALL(*mock_futility_utils, GetApWriteProtectionStatus())
          .WillByDefault(Return(args.apwp_enabled));
    } else {
      ON_CALL(*mock_futility_utils, GetApWriteProtectionStatus())
          .WillByDefault(Return(std::nullopt));
    }
    ON_CALL(*mock_futility_utils, DisableApSoftwareWriteProtection())
        .WillByDefault(Return(true));
    // Use |apwp_success| to control the return value of enabling AP SWWP.
    ON_CALL(*mock_futility_utils, EnableApSoftwareWriteProtection())
        .WillByDefault(Return(args.apwp_success));

    // Mock |GscUtils|.
    auto mock_gsc_utils = std::make_unique<NiceMock<MockGscUtils>>();
    ON_CALL(*mock_gsc_utils, GetChassisOpenStatus())
        .WillByDefault(Return(args.chassis_open));

    return std::make_unique<WriteProtectUtilsImpl>(
        std::move(mock_crossystem_utils), std::move(mock_ec_utils),
        std::move(mock_futility_utils), std::move(mock_gsc_utils));
  }
};

TEST_F(WriteProtectUtilsTest, GetHwwp_Enabled_Success) {
  auto utils = CreateWriteProtectUtils({});
  auto wp_status = utils->GetHardwareWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_TRUE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetHwwp_Disabled_Success) {
  auto utils = CreateWriteProtectUtils({.hwwp_enabled = false});
  auto wp_status = utils->GetHardwareWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_FALSE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetHwwp_Fail) {
  auto utils = CreateWriteProtectUtils({.hwwp_success = false});
  auto wp_status = utils->GetHardwareWriteProtectionStatus();
  ASSERT_FALSE(wp_status.has_value());
}

TEST_F(WriteProtectUtilsTest, GetApwp_Enabled_Success) {
  auto utils = CreateWriteProtectUtils({});
  auto wp_status = utils->GetApWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_TRUE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetApwp_Disabled_Success) {
  auto utils = CreateWriteProtectUtils({.apwp_enabled = false});
  auto wp_status = utils->GetApWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_FALSE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetApwp_Fail) {
  auto utils = CreateWriteProtectUtils({.apwp_success = false});
  auto wp_status = utils->GetApWriteProtectionStatus();
  ASSERT_FALSE(wp_status.has_value());
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Enabled_Success) {
  auto utils = CreateWriteProtectUtils({});
  auto wp_status = utils->GetEcWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_TRUE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Disabled_Success) {
  auto utils = CreateWriteProtectUtils({.ecwp_enabled = false});
  auto wp_status = utils->GetEcWriteProtectionStatus();
  ASSERT_TRUE(wp_status.has_value());
  ASSERT_FALSE(wp_status.value());
}

TEST_F(WriteProtectUtilsTest, GetEcwp_Fail) {
  auto utils = CreateWriteProtectUtils({.ecwp_success = false});
  auto wp_status = utils->GetEcWriteProtectionStatus();
  ASSERT_FALSE(wp_status.has_value());
}

TEST_F(WriteProtectUtilsTest, DisableWp_Success) {
  auto utils = CreateWriteProtectUtils({});
  ASSERT_TRUE(utils->DisableSoftwareWriteProtection());
}

TEST_F(WriteProtectUtilsTest, EnableWp_Success) {
  auto utils = CreateWriteProtectUtils({});
  ASSERT_TRUE(utils->EnableSoftwareWriteProtection());
}

TEST_F(WriteProtectUtilsTest, EnableWp_Failed_Ap) {
  auto utils = CreateWriteProtectUtils({.apwp_success = false});
  ASSERT_FALSE(utils->EnableSoftwareWriteProtection());
}

TEST_F(WriteProtectUtilsTest, EnableWp_Failed_Ec) {
  auto utils = CreateWriteProtectUtils({.ecwp_success = false});
  ASSERT_FALSE(utils->EnableSoftwareWriteProtection());
}

TEST_F(WriteProtectUtilsTest, ReadyForFactoryMode_HwwpDisabled_True) {
  auto utils = CreateWriteProtectUtils({.hwwp_enabled = false});
  ASSERT_TRUE(utils->ReadyForFactoryMode());
}

TEST_F(WriteProtectUtilsTest, ReadyForFactoryMode_ChassisOpend_True) {
  auto utils = CreateWriteProtectUtils({.chassis_open = true});
  ASSERT_TRUE(utils->ReadyForFactoryMode());
}

TEST_F(WriteProtectUtilsTest, ReadyForFactoryMode_ChassisOpend_False) {
  auto utils = CreateWriteProtectUtils({});
  ASSERT_FALSE(utils->ReadyForFactoryMode());
}

}  // namespace rmad
