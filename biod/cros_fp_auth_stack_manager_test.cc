// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_auth_stack_manager.h"

#include <utility>

#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "biod/cros_fp_device.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_cros_fp_device.h"
#include "biod/mock_cros_fp_record_manager.h"
#include "biod/power_button_filter.h"

namespace biod {

using EnrollStatus = AuthStackManager::EnrollStatus;
using Mode = ec::FpMode::Mode;

using testing::_;
using testing::Return;
using testing::SaveArg;

MATCHER_P(EnrollProgressIs, progress, "") {
  return arg.percent_complete == progress && arg.done == (progress == 100);
}

class CrosFpAuthStackManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const auto mock_bus = base::MakeRefCounted<dbus::MockBus>(options);

    const auto power_manager_proxy =
        base::MakeRefCounted<dbus::MockObjectProxy>(
            mock_bus.get(), power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    EXPECT_CALL(*mock_bus,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(testing::Return(power_manager_proxy.get()));

    auto mock_cros_dev = std::make_unique<MockCrosFpDevice>();
    // Keep a pointer to the fake device to manipulate it later.
    mock_cros_dev_ = mock_cros_dev.get();

    auto mock_record_manager = std::make_unique<MockCrosFpRecordManager>();
    // Keep a pointer to record manager, to manipulate it later.
    mock_record_manager_ = mock_record_manager.get();

    // Always support positive match secret
    EXPECT_CALL(*mock_cros_dev_, SupportsPositiveMatchSecret())
        .WillRepeatedly(Return(true));

    // Save OnMkbpEvent callback to use later in tests
    ON_CALL(*mock_cros_dev_, SetMkbpEventCallback)
        .WillByDefault(SaveArg<0>(&on_mkbp_event_));

    cros_fp_auth_stack_manager_ = std::make_unique<CrosFpAuthStackManager>(
        PowerButtonFilter::Create(mock_bus), std::move(mock_cros_dev),
        &mock_metrics_, std::move(mock_record_manager));

    cros_fp_auth_stack_manager_->SetEnrollScanDoneHandler(
        base::BindRepeating(&CrosFpAuthStackManagerTest::EnrollScanDoneHandler,
                            base::Unretained(this)));
  }

 protected:
  MOCK_METHOD(void,
              EnrollScanDoneHandler,
              (ScanResult, const EnrollStatus&, brillo::Blob));

  metrics::MockBiodMetrics mock_metrics_;
  std::unique_ptr<CrosFpAuthStackManager> cros_fp_auth_stack_manager_;
  MockCrosFpRecordManager* mock_record_manager_;
  MockCrosFpDevice* mock_cros_dev_;
  CrosFpDevice::MkbpCallback on_mkbp_event_;
};

TEST_F(CrosFpAuthStackManagerTest, TestGetType) {
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetType(), BIOMETRIC_TYPE_FINGERPRINT);
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionSuccess) {
  AuthStackManager::Session enroll_session;

  // Expect biod will check if there is space for a new template.
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));

  // Expect that biod will ask FPMCU to set the enroll mode.
  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(true));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_TRUE(enroll_session);

  // When enroll session ends, FP mode will be set to kNone.
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionTwiceFailed) {
  AuthStackManager::Session first_enroll_session;
  AuthStackManager::Session second_enroll_session;

  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillRepeatedly(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillRepeatedly(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillRepeatedly(Return(true));

  first_enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  ASSERT_TRUE(first_enroll_session);

  second_enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_FALSE(second_enroll_session);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollSessionEnrollModeFailed) {
  AuthStackManager::Session enroll_session;

  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(false));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_FALSE(enroll_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestDoEnrollImageEventSuccess) {
  const brillo::Blob kNonce(32, 1);
  // Start an enrollment sessions without performing all checks since this is
  // already tested by TestStartEnrollSessionSuccess.
  AuthStackManager::Session enroll_session;
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(_)).WillRepeatedly(Return(true));
  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  ASSERT_TRUE(enroll_session);

  EXPECT_CALL(*mock_cros_dev_, GetNonce).WillOnce(Return(kNonce));

  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_IMMOBILE,
                                    EnrollProgressIs(25), brillo::Blob()));
  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_PARTIAL,
                                    EnrollProgressIs(50), brillo::Blob()));
  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_INSUFFICIENT,
                                    EnrollProgressIs(75), brillo::Blob()));
  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_SUCCESS,
                                           EnrollProgressIs(100), kNonce));

  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_IMMOBILE |
                     25 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE |
                     50 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY |
                     75 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_OK |
                     100 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
}

}  // namespace biod
