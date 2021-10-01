// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_object_test_helpers.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <hps/daemon/dbus_adaptor.h>
#include <hps/hps.h>

using brillo::dbus_utils::AsyncEventSequencer;
using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace hps {

class MockHps : public HPS {
 public:
  MOCK_METHOD(void,
              Init,
              (uint32_t, const base::FilePath&, const base::FilePath&),
              (override));
  MOCK_METHOD(bool, Boot, (), (override));
  MOCK_METHOD(void, SkipBoot, (), (override));
  MOCK_METHOD(bool, Enable, (uint8_t), (override));
  MOCK_METHOD(bool, Disable, (uint8_t), (override));
  MOCK_METHOD(FeatureResult, Result, (int), (override));
  MOCK_METHOD(DevInterface*, Device, (), (override));
  MOCK_METHOD(bool, Download, (HpsBank, const base::FilePath&), (override));
};

class HpsDaemonTest : public testing::Test {
 public:
  HpsDaemonTest() {
    dbus::Bus::Options options;
    mock_bus_ = base::MakeRefCounted<NiceMock<dbus::MockBus>>(options);
    dbus::ObjectPath path(::hps::kHpsServicePath);

    mock_object_proxy_ = base::MakeRefCounted<NiceMock<dbus::MockObjectProxy>>(
        mock_bus_.get(), kHpsServicePath, path);

    mock_exported_object_ =
        base::MakeRefCounted<StrictMock<dbus::MockExportedObject>>(
            mock_bus_.get(), path);

    ON_CALL(*mock_bus_, GetExportedObject(path))
        .WillByDefault(Return(mock_exported_object_.get()));

    ON_CALL(*mock_bus_, GetDBusTaskRunner())
        .WillByDefault(
            Return(task_environment_.GetMainThreadTaskRunner().get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(testing::AnyNumber());

    auto hps = std::make_unique<StrictMock<MockHps>>();
    mock_hps_ = hps.get();
    hps_daemon_.reset(new DBusAdaptor(mock_bus_, std::move(hps), kPollTimeMs));

    feature_config_.set_allocated_basic_filter_config(
        new FeatureConfig_BasicFilterConfig());

    brillo_loop_.SetAsCurrent();
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  brillo::BaseMessageLoop brillo_loop_{
      task_environment_.GetMainThreadTaskRunner().get()};

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  StrictMock<MockHps>* mock_hps_;
  std::unique_ptr<DBusAdaptor> hps_daemon_;
  FeatureConfig feature_config_;
  static constexpr uint32_t kPollTimeMs = 500;
};

TEST_F(HpsDaemonTest, EnableFeatureNotReady) {
  EXPECT_CALL(*mock_hps_, Enable(0)).WillOnce(Return(false));
  brillo::ErrorPtr error;
  bool result = hps_daemon_->EnableHpsSense(&error, feature_config_);
  EXPECT_FALSE(result);
  EXPECT_EQ("hpsd: Unable to enable feature", error->GetMessage());
}

TEST_F(HpsDaemonTest, EnableFeatureReady) {
  EXPECT_CALL(*mock_hps_, Enable(0)).WillOnce(Return(true));
  brillo::ErrorPtr error;
  bool result = hps_daemon_->EnableHpsSense(&error, feature_config_);
  EXPECT_TRUE(result);
}

TEST_F(HpsDaemonTest, DisableFeatureNotReady) {
  EXPECT_CALL(*mock_hps_, Disable(0)).WillOnce(Return(false));
  brillo::ErrorPtr error;
  bool result = hps_daemon_->DisableHpsSense(&error);
  EXPECT_FALSE(result);
  EXPECT_EQ("hpsd: Unable to disable feature", error->GetMessage());
}

TEST_F(HpsDaemonTest, DisableFeatureReady) {
  EXPECT_CALL(*mock_hps_, Disable(0)).WillOnce(Return(true));
  brillo::ErrorPtr error;
  bool result = hps_daemon_->DisableHpsSense(&error);
  EXPECT_TRUE(result);
}

TEST_F(HpsDaemonTest, GetFeatureResultNotEnabled) {
  brillo::ErrorPtr error;
  bool result;

  bool call_result = hps_daemon_->GetResultHpsSense(&error, &result);
  EXPECT_FALSE(call_result);
  EXPECT_EQ("hpsd: Feature not enabled.", error->GetMessage());
}

TEST_F(HpsDaemonTest, TestPollTimer) {
  FeatureResult feature_result{.valid = true};
  {
    InSequence sequence;
    EXPECT_CALL(*mock_hps_, Enable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0))
        .Times(2)
        .WillRepeatedly(Return(feature_result));
    EXPECT_CALL(*mock_hps_, Disable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0)).Times(0);
  }

  brillo::ErrorPtr error;
  bool result = hps_daemon_->EnableHpsSense(&error, feature_config_);
  EXPECT_TRUE(result);

  // Advance timer far enough so that the poll timer should fire twice.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs * 2));

  // Disable the feature, time should no longer fire.
  result = hps_daemon_->DisableHpsSense(&error);
  EXPECT_TRUE(result);

  // Poll task should no longer fire if we advance the timer.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs * 2));
}

TEST_F(HpsDaemonTest, TestPollTimerMultipleFeatures) {
  FeatureResult feature_result{.valid = true};
  {
    InSequence sequence;
    EXPECT_CALL(*mock_hps_, Enable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Enable(1)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0)).WillOnce(Return(feature_result));
    EXPECT_CALL(*mock_hps_, Result(1)).WillOnce(Return(feature_result));
    EXPECT_CALL(*mock_hps_, Disable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0)).Times(0);
    EXPECT_CALL(*mock_hps_, Result(1)).WillOnce(Return(feature_result));
    EXPECT_CALL(*mock_hps_, Disable(1)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(1)).Times(0);
  }

  brillo::ErrorPtr error;

  // Enable features 0 & 1
  bool result = hps_daemon_->EnableHpsSense(&error, feature_config_);
  EXPECT_TRUE(result);
  result = hps_daemon_->EnableHpsNotify(&error, feature_config_);
  EXPECT_TRUE(result);

  // Advance timer far enough so that the poll timer should fire.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));

  // Disable the feature, timer should no longer fire for feature 0.
  result = hps_daemon_->DisableHpsSense(&error);
  EXPECT_TRUE(result);

  // Advance timer far enough so that the poll timer should fire.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));

  // Disable the feature, timer should no longer fire for feature 1.
  result = hps_daemon_->DisableHpsNotify(&error);
  EXPECT_TRUE(result);

  // Advance time to ensure no more features are firing.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));
}

// TODO(slangley): Work out how to check that the signal was fired, on first
// inspection it doesn't come via the mocks we have.
TEST_F(HpsDaemonTest, DISABLED_TestSignals) {
  // This result indicates a positive inference from HPS.
  FeatureResult valid_feature_result{.inference_result = 254, .valid = true};
  FeatureResult invalid_feature_result{.inference_result = 254, .valid = false};
  {
    InSequence sequence;
    EXPECT_CALL(*mock_hps_, Enable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Enable(1)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0)).WillOnce(Return(valid_feature_result));
    EXPECT_CALL(*mock_hps_, Result(1)).WillOnce(Return(valid_feature_result));
    EXPECT_CALL(*mock_hps_, Result(0)).WillOnce(Return(invalid_feature_result));
    EXPECT_CALL(*mock_hps_, Result(1)).WillOnce(Return(invalid_feature_result));
    EXPECT_CALL(*mock_hps_, Disable(0)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Disable(1)).WillOnce(Return(true));
    EXPECT_CALL(*mock_hps_, Result(0)).Times(0);
    EXPECT_CALL(*mock_hps_, Result(1)).Times(0);
  }

  brillo::ErrorPtr error;

  // Enable features 0 & 1
  bool result = hps_daemon_->EnableHpsSense(&error, feature_config_);
  EXPECT_TRUE(result);
  result = hps_daemon_->EnableHpsNotify(&error, feature_config_);
  EXPECT_TRUE(result);

  // Advance timer far enough so that the poll timer should fire.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));

  // Advance timer far enough so that the poll timer should fire again.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));

  // Disable the feature, timer should no longer fire for feature 0.
  result = hps_daemon_->DisableHpsSense(&error);
  EXPECT_TRUE(result);

  // Disable the feature, timer should no longer fire for feature 1.
  result = hps_daemon_->DisableHpsNotify(&error);
  EXPECT_TRUE(result);

  // Advance time to ensure no more features are firing.
  task_environment_.FastForwardBy(
      base::TimeDelta::FromMilliseconds(kPollTimeMs));
}

}  // namespace hps
