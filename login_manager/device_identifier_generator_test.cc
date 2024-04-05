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

constexpr char kSerialNumberKeyName[] = "serial_number";
constexpr char kDiskSerialNumberKeyName[] = "root_disk_serial_number";
constexpr char kStableDeviceSecretKeyName[] =
    "stable_device_secret_DO_NOT_SHARE";
constexpr char kReEnrollmentKeyName[] = "re_enrollment_key";

constexpr char kStableDeviceSecret[] =
    "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";
constexpr char kReEnrollmentKey[] =
    "0011223344556677889900aabbccddeeff112233445566778899aabbccddeeff"
    "0011223344556677889900aabbccddeeff112233445566778899aabbccddeeff";

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
      kDiskSerialNumberKeyName, kDiskSerialNumberKeyName);
  std::map<std::string, std::string> map;
  const std::map<std::string, std::string> ro_vpd;
  const std::map<std::string, std::string> rw_vpd = {
      {kDiskSerialNumberKeyName, "sn_3"}};
  DeviceIdentifierGenerator::ParseMachineInfo(machine_info_file_contents,
                                              ro_vpd, rw_vpd, &map);
  EXPECT_EQ("sn_1", map[kDiskSerialNumberKeyName]);
}

TEST(DeviceIdentifierGeneratorStaticTest, ParseMachineInfoSuccess) {
  std::map<std::string, std::string> params;
  const std::map<std::string, std::string> ro_vpd = {
      {kSerialNumberKeyName, "fake-machine-serial-number"},
      {kDiskSerialNumberKeyName, "IGNORE THIS ONE - IT'S NOT FROM UDEV"},
      {kStableDeviceSecretKeyName, kStableDeviceSecret},
  };
  const std::map<std::string, std::string> rw_vpd = {
      {kSerialNumberKeyName, "key collision"},
  };
  EXPECT_TRUE(DeviceIdentifierGenerator::ParseMachineInfo(
      base::StringPrintf("\"%s\"=\"fake disk-serial-number\"\n"
                         "%s=\"%s\"\n",  // No quoting of the key name.
                         kDiskSerialNumberKeyName, kReEnrollmentKeyName,
                         kReEnrollmentKey),
      ro_vpd, rw_vpd, &params));
  EXPECT_EQ(4, params.size());
  EXPECT_EQ("fake-machine-serial-number", params[kSerialNumberKeyName]);
  EXPECT_EQ("fake disk-serial-number", params[kDiskSerialNumberKeyName]);
  EXPECT_EQ(kReEnrollmentKey, params[kReEnrollmentKeyName]);
}

TEST(DeviceIdentifierGeneratorStaticTest, ParseMachineInfoFailure) {
  std::map<std::string, std::string> params;
  const std::map<std::string, std::string> ro_vpd;
  const std::map<std::string, std::string> rw_vpd;
  EXPECT_FALSE(DeviceIdentifierGenerator::ParseMachineInfo("bad!", ro_vpd,
                                                           rw_vpd, &params));
}

class DeviceIdentifierGeneratorTest : public testing::Test {
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

 protected:
  base::test::SingleThreadTaskEnvironment task_environment;

  FakeSystemUtils system_utils_;
  MockMetrics metrics_;

  DeviceIdentifierGenerator generator_;

  LoginMetrics::StateKeyGenerationStatus last_state_key_generation_status_;
};

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
  params[kDiskSerialNumberKeyName] = "fake-disk-serial-number";
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
  params[kSerialNumberKeyName] = "fake-machine-serial-number";
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
      {kStableDeviceSecretKeyName, "not a hex number"}};
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

TEST_F(DeviceIdentifierGeneratorTest, MalformedReEnrollmentKey) {
  const std::map<std::string, std::string> params{
      {kReEnrollmentKeyName, "not a hex number"}};
  ASSERT_TRUE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_BAD_RE_ENROLLMENT_KEY,
            last_state_key_generation_status_);
  EXPECT_EQ(
      state_keys,
      base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                           kMalformedReEnrollmentKey));
}

TEST_F(DeviceIdentifierGeneratorTest, MalformedReEnrollmentKeyTooShort) {
  const std::map<std::string, std::string> params{
      {kReEnrollmentKeyName, std::string(kReEnrollmentKey).substr(0, 127)}};
  ASSERT_TRUE(generator_.InitMachineInfo(params));

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(LoginMetrics::STATE_KEY_STATUS_BAD_RE_ENROLLMENT_KEY,
            last_state_key_generation_status_);
  EXPECT_EQ(
      state_keys,
      base::unexpected(DeviceIdentifierGenerator::StateKeysComputationError::
                           kMalformedReEnrollmentKey));
}

struct GeneratorParams {
  std::map<std::string, std::string> machine_info_params;
  LoginMetrics::StateKeyGenerationStatus generation_status;
  size_t num_state_keys;
};

class DeviceIdentifierGeneratorTestP
    : public DeviceIdentifierGeneratorTest,
      public testing::WithParamInterface<GeneratorParams> {
 public:
  DeviceIdentifierGeneratorTestP() : DeviceIdentifierGeneratorTest() {}

 protected:
  // Installs mock data for the required parameters.
  void InitMachineInfo() {
    ASSERT_TRUE(generator_.InitMachineInfo(GetParam().machine_info_params));
  }

  void CompletionPsmDeviceKeyHandler(const std::string& derived_secret) {
    psm_device_secret_received_ = true;
    psm_derived_secret_ = derived_secret;
  }

  void RequestPsmDeviceActiveSecret(bool expect_immediate_callback) {
    psm_device_secret_received_ = false;
    generator_.RequestPsmDeviceActiveSecret(base::BindOnce(
        &DeviceIdentifierGeneratorTestP::CompletionPsmDeviceKeyHandler,
        base::Unretained(this)));
    EXPECT_EQ(expect_immediate_callback, psm_device_secret_received_);
  }

  bool psm_device_secret_received_ = false;
  std::string psm_derived_secret_;
};

TEST_P(DeviceIdentifierGeneratorTestP, PendingMachineInfo) {
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
  EXPECT_EQ(GetParam().num_state_keys, state_keys.value().size());

  // Pending callbacks are fired and discarded.
  EXPECT_TRUE(generator_.GetPendingCallbacksForTesting().empty());
}

TEST_P(DeviceIdentifierGeneratorTestP, RequestStateKeys) {
  InitMachineInfo();

  base::test::TestFuture<const StateKeysResult&> state_keys_future;
  generator_.RequestStateKeys(state_keys_future.GetCallback());
  ASSERT_TRUE(state_keys_future.IsReady());
  StateKeysResult state_keys = state_keys_future.Get();

  EXPECT_EQ(GetParam().generation_status, last_state_key_generation_status_);
  ASSERT_TRUE(state_keys.has_value());
  EXPECT_EQ(GetParam().num_state_keys, state_keys.value().size());
}

TEST_P(DeviceIdentifierGeneratorTestP,
       RequestPsmDeviceActiveSecretSuccessAfterInitMachineInfo) {
  InitMachineInfo();
  RequestPsmDeviceActiveSecret(true);
  EXPECT_TRUE(psm_device_secret_received_);
}

TEST_P(DeviceIdentifierGeneratorTestP,
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

TEST_P(DeviceIdentifierGeneratorTestP, TimedStateKeys) {
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

  EXPECT_EQ(GetParam().generation_status, last_state_key_generation_status_);
  ASSERT_TRUE(initial_state_keys.has_value());
  EXPECT_EQ(GetParam().num_state_keys, initial_state_keys.value().size());

  // All state keys are different.
  std::set<DeviceIdentifierGenerator::StateKeysList::value_type> state_key_set(
      initial_state_keys.value().begin(), initial_state_keys.value().end());
  EXPECT_EQ(GetParam().num_state_keys, initial_state_keys.value().size());

  // Moving forward just a little yields the same keys.
  system_utils_.forward_time(base::Days(1).InSeconds());

  StateKeysResult second_state_keys;

  {
    base::test::TestFuture<const StateKeysResult&> state_keys_future;
    generator_.RequestStateKeys(state_keys_future.GetCallback());
    ASSERT_TRUE(state_keys_future.IsReady());
    second_state_keys = state_keys_future.Get();
  }

  EXPECT_EQ(GetParam().generation_status, last_state_key_generation_status_);
  EXPECT_EQ(initial_state_keys, second_state_keys);

  // If we expect only one state key, there are no quanta, so return now.
  if (GetParam().num_state_keys == 1) {
    return;
  }

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

  EXPECT_EQ(GetParam().generation_status, last_state_key_generation_status_);
  ASSERT_TRUE(future_state_keys.has_value());
  EXPECT_EQ(GetParam().num_state_keys, future_state_keys.value().size());
  EXPECT_TRUE(std::equal(initial_state_keys.value().begin() + 2,
                         initial_state_keys.value().end(),
                         future_state_keys.value().begin()));
}

INSTANTIATE_TEST_SUITE_P(
    DeviceIdentifierGeneratorTestSuite,
    DeviceIdentifierGeneratorTestP,
    testing::Values(
        GeneratorParams(
            {{kReEnrollmentKeyName, kReEnrollmentKey}},
            LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_RE_ENROLLMENT_KEY,
            1),
        GeneratorParams(
            {{kStableDeviceSecretKeyName, kStableDeviceSecret}},
            LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET,
            DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta),
        GeneratorParams(
            {
                {kSerialNumberKeyName, "fake-machine-serial-number"},
                {kDiskSerialNumberKeyName, "fake-disk-serial-number"},
            },
            LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_IDENTIFIER_HASH,
            DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta)));

}  // namespace login_manager
