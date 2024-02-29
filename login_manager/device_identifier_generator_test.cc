// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_identifier_generator.h"

#include <stdint.h>
#include <stdlib.h>

#include <map>
#include <set>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/login_metrics.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/system_utils_impl.h"

using testing::_;
using testing::SaveArg;

namespace login_manager {

namespace {

constexpr char kSerialNumberKey[] = "serial_number";
constexpr char kDiskSerialNumberKey[] = "root_disk_serial_number";
constexpr char kStableDeviceSecretKey[] = "stable_device_secret_DO_NOT_SHARE";

// A SystemUtils implementation that mocks time.
class FakeSystemUtils : public SystemUtilsImpl {
 public:
  FakeSystemUtils() : time_(0) {}
  FakeSystemUtils(const FakeSystemUtils&) = delete;
  FakeSystemUtils& operator=(const FakeSystemUtils&) = delete;

  ~FakeSystemUtils() override {}

  time_t time(time_t* t) override {
    if (t)
      *t = time_;
    return time_;
  }

  void forward_time(time_t offset) { time_ += offset; }

 private:
  // Current time.
  time_t time_;
};

}  // namespace

TEST(DeviceIdentifierGeneratorStaticTest,
     ParseMachineInfoRecordsFirstValueForDuplicatedKey) {
  const std::string machine_info_file_contents = base::StringPrintf(
      "\"%s\"=\"sn_1\"\n"
      "\"%s\"=\"sn_2\"\n",
      kDiskSerialNumberKey, kDiskSerialNumberKey);
  std::map<std::string, std::string> map;
  const std::map<std::string, std::string> ro_vpd;
  const std::map<std::string, std::string> rw_vpd = {
      {kDiskSerialNumberKey, "sn_3"}};
  DeviceIdentifierGenerator::ParseMachineInfo(machine_info_file_contents,
                                              ro_vpd, rw_vpd, &map);
  EXPECT_EQ("sn_1", map[kDiskSerialNumberKey]);
}

TEST(DeviceIdentifierGeneratorStaticTest, ParseMachineInfoSuccess) {
  std::map<std::string, std::string> params;
  const std::map<std::string, std::string> ro_vpd = {
      {kSerialNumberKey, "fake-machine-serial-number"},
      {kDiskSerialNumberKey, "IGNORE THIS ONE - IT'S NOT FROM UDEV"},
      {kStableDeviceSecretKey,
       "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff"},
  };
  const std::map<std::string, std::string> rw_vpd = {
      {kSerialNumberKey, "key collision"},
  };
  EXPECT_TRUE(DeviceIdentifierGenerator::ParseMachineInfo(
      base::StringPrintf("\"%s\"=\"fake disk-serial-number\"\n",
                         kDiskSerialNumberKey),
      ro_vpd, rw_vpd, &params));
  EXPECT_EQ(3, params.size());
  EXPECT_EQ("fake-machine-serial-number", params[kSerialNumberKey]);
  EXPECT_EQ("fake disk-serial-number", params[kDiskSerialNumberKey]);
}

TEST(DeviceIdentifierGeneratorStaticTest, ParseMachineInfoFailure) {
  std::map<std::string, std::string> params;
  const std::map<std::string, std::string> ro_vpd;
  const std::map<std::string, std::string> rw_vpd;
  EXPECT_FALSE(DeviceIdentifierGenerator::ParseMachineInfo("bad!", ro_vpd,
                                                           rw_vpd, &params));
}

class DeviceIdentifierGeneratorTest : public ::testing::Test {
 public:
  using StateKeysResult =
      base::expected<DeviceIdentifierGenerator::StateKeysList,
                     DeviceIdentifierGenerator::StateKeysComputationError>;

  DeviceIdentifierGeneratorTest()
      : generator_(&system_utils_, &metrics_),
        last_state_key_generation_status_(
            LoginMetrics::DEPRECATED_STATE_KEY_STATUS_MISSING_IDENTIFIERS) {
    EXPECT_CALL(metrics_, SendStateKeyGenerationStatus(_))
        .WillRepeatedly(SaveArg<0>(&last_state_key_generation_status_));
  }
  DeviceIdentifierGeneratorTest(const DeviceIdentifierGeneratorTest&) = delete;
  DeviceIdentifierGeneratorTest& operator=(
      const DeviceIdentifierGeneratorTest&) = delete;

  ~DeviceIdentifierGeneratorTest() override {}

  // Installs mock data for the required parameters.
  void InitMachineInfo() {
    std::map<std::string, std::string> params;
    params[kSerialNumberKey] = "fake-machine-serial-number";
    params[kDiskSerialNumberKey] = "fake-disk-serial-number";
    params[kStableDeviceSecretKey] =
        "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";
    ASSERT_TRUE(generator_.InitMachineInfo(params));
  }

  void CompletionPsmDeviceKeyHandler(const std::string& derived_secret) {
    psm_device_secret_received_ = true;
    psm_derived_secret_ = derived_secret;
  }

  void RequestPsmDeviceActiveSecret(bool expect_immediate_callback) {
    psm_device_secret_received_ = false;
    generator_.RequestPsmDeviceActiveSecret(base::BindOnce(
        &DeviceIdentifierGeneratorTest::CompletionPsmDeviceKeyHandler,
        base::Unretained(this)));
    EXPECT_EQ(expect_immediate_callback, psm_device_secret_received_);
  }

  base::test::SingleThreadTaskEnvironment task_environment;

  FakeSystemUtils system_utils_;
  MockMetrics metrics_;

  DeviceIdentifierGenerator generator_;

  bool psm_device_secret_received_;
  std::string psm_derived_secret_;

  LoginMetrics::StateKeyGenerationStatus last_state_key_generation_status_;
};

TEST_F(DeviceIdentifierGeneratorTest, RequestStateKeys) {
  InitMachineInfo();

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET,
            last_state_key_generation_status_);
  ASSERT_TRUE(state_keys.has_value());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            state_keys.value().size());
}

TEST_F(DeviceIdentifierGeneratorTest,
       RequestPsmDeviceActiveSecretSuccessAfterInitMachineInfo) {
  InitMachineInfo();
  RequestPsmDeviceActiveSecret(true);
  EXPECT_TRUE(psm_device_secret_received_);
}

TEST_F(DeviceIdentifierGeneratorTest,
       RequestPsmDeviceActiveSecretSuccessBeforeInitMachineInfo) {
  // No callback as long as machine info has not been provided.
  RequestPsmDeviceActiveSecret(false);
  InitMachineInfo();
  EXPECT_TRUE(psm_device_secret_received_);

  // Sending machine info twice is harmless and doesn't fire callbacks.
  psm_device_secret_received_ = false;
  InitMachineInfo();
  EXPECT_FALSE(psm_device_secret_received_);
}

TEST_F(DeviceIdentifierGeneratorTest, RequestStateKeysLegacy) {
  std::map<std::string, std::string> params;
  params[kSerialNumberKey] = "fake-machine-serial-number";
  params[kDiskSerialNumberKey] = "fake-disk-serial-number";
  ASSERT_TRUE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_IDENTIFIER_HASH,
            last_state_key_generation_status_);
  ASSERT_TRUE(state_keys.has_value());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            state_keys.value().size());
}

TEST_F(DeviceIdentifierGeneratorTest, TimedStateKeys) {
  InitMachineInfo();
  system_utils_.forward_time(base::Days(100).InSeconds());

  // The correct number of state keys gets returned.
  StateKeysResult initial_state_keys;

  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    ASSERT_TRUE(state_keys_future.IsReady());
    initial_state_keys = state_keys_future.Get();
  }

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET,
            last_state_key_generation_status_);
  ASSERT_TRUE(initial_state_keys.has_value());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            initial_state_keys.value().size());

  // All state keys are different.
  std::set<DeviceIdentifierGenerator::StateKeysList::value_type> state_key_set(
      initial_state_keys.value().begin(), initial_state_keys.value().end());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            state_key_set.size());

  // Moving forward just a little yields the same keys.
  system_utils_.forward_time(base::Days(1).InSeconds());

  StateKeysResult second_state_keys;

  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    ASSERT_TRUE(state_keys_future.IsReady());
    second_state_keys = state_keys_future.Get();
  }

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET,
            last_state_key_generation_status_);
  EXPECT_EQ(initial_state_keys, second_state_keys);

  // Jumping to a future quantum results in the state keys rolling forward.
  int64_t step =
      1 << DeviceIdentifierGenerator::kDeviceStateKeyTimeQuantumPower;
  system_utils_.forward_time(2 * step);

  StateKeysResult future_state_keys;

  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    ASSERT_TRUE(state_keys_future.IsReady());
    future_state_keys = state_keys_future.Get();
  }

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET,
            last_state_key_generation_status_);
  ASSERT_TRUE(future_state_keys.has_value());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            future_state_keys.value().size());
  EXPECT_TRUE(std::equal(initial_state_keys.value().begin() + 2,
                         initial_state_keys.value().end(),
                         future_state_keys.value().begin()));
}

TEST_F(DeviceIdentifierGeneratorTest, PendingMachineInfo) {
  // No callback as long as machine info has not been provided.
  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(state_keys_future.IsReady());

  // Supplying machine info fires callbacks.
  InitMachineInfo();

  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();
  ASSERT_TRUE(state_keys.has_value());
  EXPECT_EQ(DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta,
            state_keys.value().size());

  // Pending callbacks are fired and discarded.
  EXPECT_TRUE(generator_.GetPendingCallbacksForTesting().empty());
}

TEST_F(DeviceIdentifierGeneratorTest, PendingMachineInfoFailure) {
  // No callback as long as machine info has not been provided.
  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    base::RunLoop().RunUntilIdle();
    ASSERT_FALSE(state_keys_future.IsReady());

    // Supplying machine info fires callbacks even if info is missing.
    std::map<std::string, std::string> empty;
    EXPECT_FALSE(generator_.InitMachineInfo(empty));

    ASSERT_TRUE(state_keys_future.IsReady());
    StateKeysResult state_keys = state_keys_future.Get();
    EXPECT_EQ(
        state_keys,
        base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                             kMissingAllDeviceIdentifiers));
  }

  // Later requests get answered immediately.
  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    ASSERT_TRUE(state_keys_future.IsReady());
    StateKeysResult state_keys = state_keys_future.Get();

    EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_MISSING_ALL_IDENTIFIERS,
              last_state_key_generation_status_);
    EXPECT_EQ(
        state_keys,
        base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                             kMissingAllDeviceIdentifiers));
  }
}

TEST_F(DeviceIdentifierGeneratorTest, MissingMachineSerialNumber) {
  std::map<std::string, std::string> params;
  params[kDiskSerialNumberKey] = "fake-disk-serial-number";
  ASSERT_FALSE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_MISSING_MACHINE_SERIAL_NUMBER,
            last_state_key_generation_status_);
  EXPECT_EQ(
      state_keys,
      base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                           kMissingSerialNumber));
}

TEST_F(DeviceIdentifierGeneratorTest, MissingDiskSerialNumber) {
  std::map<std::string, std::string> params;
  params[kSerialNumberKey] = "fake-machine-serial-number";
  ASSERT_FALSE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_MISSING_DISK_SERIAL_NUMBER,
            last_state_key_generation_status_);
  EXPECT_EQ(
      state_keys,
      base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                           kMissingDiskSerialNumber));
}

TEST_F(DeviceIdentifierGeneratorTest, MalformedDeviceSecret) {
  const std::map<std::string, std::string> params{
      {kStableDeviceSecretKey, "not a hex number"}};
  ASSERT_TRUE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_BAD_DEVICE_SECRET,
            last_state_key_generation_status_);
  EXPECT_EQ(
      state_keys,
      base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                           kMalformedDeviceSecret));
}

}  // namespace login_manager
