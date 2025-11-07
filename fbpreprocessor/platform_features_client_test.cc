// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/platform_features_client.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/task_environment.h>
#include <brillo/files/file_util.h>
#include <dbus/mock_bus.h>
#include <featured/fake_platform_features.h>
#include <featured/feature_library.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Return;

namespace fbpreprocessor {
namespace {

constexpr char kAllowFirmwareDumpsFlagPath[] = "allow_firmware_dumps";
constexpr char kAllowFirmwareDumpsFeatureName[] =
    "CrOSLateBootAllowFirmwareDumps";

class PlatformFeaturesClientTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::MockBus>(std::move(options));

    CHECK(test_dir_.CreateUniqueTempDir());

    fake_platform_features_ =
        std::make_unique<feature::FakePlatformFeatures>(bus_);

    EXPECT_CALL(*bus_, GetOriginTaskRunner())
        .WillRepeatedly(
            Return(base::SequencedTaskRunner::GetCurrentDefault().get()));

    client_.set_base_dir_for_test(test_dir_.GetPath());
    client_.Start(fake_platform_features_.get());
    task_environment_.RunUntilIdle();
  }

  void TearDown() override { fake_platform_features_->ShutdownBus(); }

  void SetIsFeatureEnabledWithRefetch(bool enabled) {
    fake_platform_features_->SetEnabled(kAllowFirmwareDumpsFeatureName,
                                        enabled);
    fake_platform_features_->TriggerRefetchSignal();
    task_environment_.RunUntilIdle();
  }

  base::FilePath GetAllowFirmwareDumpsFlagPath() const {
    return test_dir_.GetPath().Append(
        base::FilePath(kAllowFirmwareDumpsFlagPath));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  scoped_refptr<dbus::MockBus> bus_;

  base::ScopedTempDir test_dir_;

  std::unique_ptr<feature::FakePlatformFeatures> fake_platform_features_;

  PlatformFeaturesClient client_;
};

class MockObserver : public PlatformFeaturesClient::Observer {
 public:
  MOCK_METHOD(void, OnFeatureChanged, (bool allowed));
};

TEST_F(PlatformFeaturesClientTest, FeatureAllowed) {
  SetIsFeatureEnabledWithRefetch(true);
  EXPECT_TRUE(client_.FirmwareDumpsAllowedByFinch());
  std::string file_content;
  EXPECT_TRUE(
      base::ReadFileToString(GetAllowFirmwareDumpsFlagPath(), &file_content));
  EXPECT_EQ(file_content, "1");
}

TEST_F(PlatformFeaturesClientTest, FeatureAllowedByDefault) {
  EXPECT_TRUE(client_.FirmwareDumpsAllowedByFinch());
  std::string file_content;
  EXPECT_TRUE(
      base::ReadFileToString(GetAllowFirmwareDumpsFlagPath(), &file_content));
  EXPECT_EQ(file_content, "1");
}

TEST_F(PlatformFeaturesClientTest, FeatureAllowedFlagFileUpdated) {
  // Delete the file to avoid being tricked by leftover state from the init
  // phase.
  EXPECT_TRUE(brillo::DeleteFile(GetAllowFirmwareDumpsFlagPath()));

  SetIsFeatureEnabledWithRefetch(true);
  std::string file_content;
  EXPECT_TRUE(
      base::ReadFileToString(GetAllowFirmwareDumpsFlagPath(), &file_content));
  EXPECT_EQ(file_content, "1");
}

TEST_F(PlatformFeaturesClientTest, FeatureDisallowed) {
  SetIsFeatureEnabledWithRefetch(false);
  EXPECT_FALSE(client_.FirmwareDumpsAllowedByFinch());
  std::string file_content;
  EXPECT_TRUE(
      base::ReadFileToString(GetAllowFirmwareDumpsFlagPath(), &file_content));
  EXPECT_EQ(file_content, "0");
}

TEST_F(PlatformFeaturesClientTest, ObserverCalled) {
  MockObserver observer;
  client_.AddObserver(&observer);
  EXPECT_CALL(observer, OnFeatureChanged(true));
  SetIsFeatureEnabledWithRefetch(true);
}

TEST_F(PlatformFeaturesClientTest, ObserverNotCalledAfterRemoval) {
  MockObserver observer;
  client_.AddObserver(&observer);
  EXPECT_CALL(observer, OnFeatureChanged(true));
  SetIsFeatureEnabledWithRefetch(true);
  client_.RemoveObserver(&observer);
  // The observer has been removed, |OnFeatureChanged()| should not be called
  // anymore.
  EXPECT_CALL(observer, OnFeatureChanged(_)).Times(0);
  SetIsFeatureEnabledWithRefetch(true);
}

TEST_F(PlatformFeaturesClientTest, ObserverCalledAfterRefetch) {
  MockObserver observer;
  client_.AddObserver(&observer);
  EXPECT_CALL(observer, OnFeatureChanged(true)).Times(1);
  SetIsFeatureEnabledWithRefetch(true);
}

TEST_F(PlatformFeaturesClientTest, ObserverCalledMultipleTimes) {
  MockObserver observer;
  client_.AddObserver(&observer);

  // First fetch, enabled.
  EXPECT_CALL(observer, OnFeatureChanged(true)).Times(1);
  SetIsFeatureEnabledWithRefetch(true);

  // Second fetch, disabled. This simulates the feature changing while
  // the program is running.
  EXPECT_CALL(observer, OnFeatureChanged(false)).Times(1);
  SetIsFeatureEnabledWithRefetch(false);

  // Third fetch, enabled again.
  EXPECT_CALL(observer, OnFeatureChanged(true)).Times(1);
  SetIsFeatureEnabledWithRefetch(true);
}

TEST_F(PlatformFeaturesClientTest, MultipleObservers) {
  MockObserver observer1;
  MockObserver observer2;
  client_.AddObserver(&observer1);
  client_.AddObserver(&observer2);
  EXPECT_CALL(observer1, OnFeatureChanged(true)).Times(1);
  EXPECT_CALL(observer2, OnFeatureChanged(true)).Times(1);
  SetIsFeatureEnabledWithRefetch(true);
}
}  // namespace
}  // namespace fbpreprocessor
