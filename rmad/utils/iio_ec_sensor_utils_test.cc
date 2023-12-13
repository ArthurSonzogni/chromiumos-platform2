// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "libmems/test_fakes.h"
#include "mojo/core/embedder/embedder.h"
#include "rmad/utils/iio_ec_sensor_utils_impl.h"
#include "rmad/utils/mock_mojo_service_utils.h"
#include "rmad/utils/mock_sensor_device.h"
#include "rmad/utils/mojo_service_utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

namespace {

constexpr char kDeviceLocationName[] = "location";
constexpr char kDeviceScaleName[] = "scale";
constexpr char kSamplingFrequencyAvailable[] = "sampling_frequency_available";
constexpr char kTestIioSysfsPrefix[] = "iio_test_";

const std::vector<std::string> kTestChannels = {"channel1", "channel2",
                                                "channel3"};
constexpr int kTestSamples = 3;
constexpr int kNumberFirstReadsDiscarded = 10;

}  // namespace

namespace rmad {

class IioEcSensorUtilsImplTest : public testing::Test {
 public:
  IioEcSensorUtilsImplTest() = default;

  struct FakeDeviceArgs {
    int id = 0;
    std::string location = "base";
    std::string name = "cros-ec-accel";
    std::string available_freq_str = "0.0 12.5 100.0";
    std::optional<double> scale = 1.0;
  };

  std::unique_ptr<libmems::fakes::FakeIioDevice> CreateFakeIioDevice(
      const FakeDeviceArgs& args) {
    auto device = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, args.name, args.id);

    EXPECT_TRUE(
        device->WriteStringAttribute(kDeviceLocationName, args.location));

    EXPECT_TRUE(device->WriteStringAttribute(kSamplingFrequencyAvailable,
                                             args.available_freq_str));

    if (args.scale.has_value()) {
      EXPECT_TRUE(
          device->WriteDoubleAttribute(kDeviceScaleName, args.scale.value()));
    }

    return device;
  }

  std::unique_ptr<libmems::fakes::FakeIioContext> CreateFakeIioContext(
      const std::vector<FakeDeviceArgs>& fake_devices) {
    auto iio_context = std::make_unique<libmems::fakes::FakeIioContext>();

    for (const auto& device_args : fake_devices) {
      auto device = CreateFakeIioDevice(device_args);
      iio_context->AddDevice(std::move(device));
    }

    return iio_context;
  }

  std::unique_ptr<IioEcSensorUtilsImpl> CreateIioEcSensorUtils(
      std::unique_ptr<libmems::fakes::FakeIioContext> iio_context,
      const std::string& location,
      const std::string& name) {
    return std::make_unique<IioEcSensorUtilsImpl>(
        mojo_service_, location, name,
        temp_dir_.GetPath().Append(kTestIioSysfsPrefix).MaybeAsASCII(),
        std::move(iio_context));
  }

  std::unique_ptr<IioEcSensorUtilsImpl> CreateIioEcSensorUtils(
      scoped_refptr<MojoServiceUtils> mojo_service,
      std::unique_ptr<libmems::fakes::FakeIioContext> iio_context,
      const std::string& location,
      const std::string& name) {
    return std::make_unique<IioEcSensorUtilsImpl>(
        mojo_service, location, name,
        temp_dir_.GetPath().Append(kTestIioSysfsPrefix).MaybeAsASCII(),
        std::move(iio_context));
  }

  void WriteSysEntries(int id, std::map<std::string, std::string> entries) {
    auto base_path = temp_dir_.GetPath().AppendASCII(kTestIioSysfsPrefix +
                                                     base::NumberToString(id));
    EXPECT_TRUE(base::CreateDirectory(base_path));
    for (const auto& [key, value] : entries) {
      auto entry_path = base_path.AppendASCII(key);
      base::WriteFile(entry_path, value);
    }
  }

 protected:
  void SetUp() override {
    mojo::core::Init();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  scoped_refptr<MojoServiceUtils> mojo_service_;
  base::ScopedTempDir temp_dir_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(IioEcSensorUtilsImplTest, Initialize_Success) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "base", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_EQ(iio_ec_sensor_utils->GetLocation(), "base");
  EXPECT_EQ(iio_ec_sensor_utils->GetName(), "cros-ec-accel");
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_NotMatched_Failed) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "lid", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_InvalidFrequency_Failed) {
  auto iio_context = CreateFakeIioContext({{.id = 0,
                                            .location = "base",
                                            .name = "cros-ec-accel",
                                            .available_freq_str = ""}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, Initialize_NoScale_Failed) {
  auto iio_context = CreateFakeIioContext({{.id = 0,
                                            .location = "base",
                                            .name = "cros-ec-accel",
                                            .scale = std::nullopt}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  EXPECT_FALSE(iio_ec_sensor_utils->IsInitialized());
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_Int_Success) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "base", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  std::map<std::string, std::string> sys_entries = {{"test_entry_1", "1"},
                                                    {"test_entry_2", "2"}};
  std::vector<std::string> keys;
  for (const auto& [key, _] : sys_entries) {
    keys.push_back(key);
  }
  WriteSysEntries(/*id=*/0, sys_entries);

  std::vector<double> expected = {1.0, 2.0};
  std::vector<double> values;
  EXPECT_TRUE(iio_ec_sensor_utils->GetSysValues(keys, &values));
  EXPECT_EQ(values.size(), keys.size());
  for (int i = 0; i < keys.size(); i++) {
    EXPECT_DOUBLE_EQ(values[i], expected[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_Float_Success) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "base", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  std::map<std::string, std::string> sys_entries = {{"test_entry_1", "1.0"},
                                                    {"test_entry_2", "2.0"}};
  std::vector<std::string> keys;
  for (const auto& [key, _] : sys_entries) {
    keys.push_back(key);
  }
  WriteSysEntries(/*id=*/0, sys_entries);

  std::vector<double> expected = {1.0, 2.0};
  std::vector<double> values;
  EXPECT_TRUE(iio_ec_sensor_utils->GetSysValues(keys, &values));
  EXPECT_EQ(values.size(), keys.size());
  for (int i = 0; i < keys.size(); i++) {
    EXPECT_DOUBLE_EQ(values[i], expected[i]);
  }
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_NotInitialized_Failed) {
  // No matched device will cause `NotInitialized`.
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "lid", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  std::map<std::string, std::string> sys_entries = {{"test_entry_1", "1.0"},
                                                    {"test_entry_2", "2.0"}};
  std::vector<std::string> keys;
  for (const auto& [key, _] : sys_entries) {
    keys.push_back(key);
  }
  WriteSysEntries(/*id=*/0, sys_entries);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues(keys, &values));
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_EntryNotAvailable_Failed) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "base", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  std::map<std::string, std::string> sys_entries = {{"test_entry_1", "1.0"},
                                                    {"test_entry_2", "2.0"}};
  WriteSysEntries(/*id=*/0, sys_entries);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues({"invalid_entry"}, &values));
}

TEST_F(IioEcSensorUtilsImplTest, GetSysValue_InvalidValue_Failed) {
  auto iio_context = CreateFakeIioContext(
      {{.id = 0, .location = "base", .name = "cros-ec-accel"}});
  auto iio_ec_sensor_utils =
      CreateIioEcSensorUtils(std::move(iio_context), "base", "cros-ec-accel");

  std::map<std::string, std::string> sys_entries = {
      {"test_entry_1", "invalid value"}, {"test_entry_2", "2.0"}};
  std::vector<std::string> keys;
  for (const auto& [key, _] : sys_entries) {
    keys.push_back(key);
  }

  WriteSysEntries(/*id=*/0, sys_entries);

  std::vector<double> values;
  EXPECT_FALSE(iio_ec_sensor_utils->GetSysValues(keys, &values));
}

TEST_F(IioEcSensorUtilsImplTest, GetAvgData_Success) {
  auto mock_mojo_service =
      base::MakeRefCounted<StrictMock<MockMojoServiceUtils>>();
  auto mock_sensor_device = StrictMock<MockSensorDevice>();

  EXPECT_CALL(mock_sensor_device, GetAllChannelIds(_))
      .Times(1)
      .WillOnce(WithArg<0>(
          [](base::OnceCallback<void(const std::vector<std::string>&)> cb) {
            std::move(cb).Run(kTestChannels);
          }));
  EXPECT_CALL(mock_sensor_device, SetTimeout(_)).Times(1);
  EXPECT_CALL(mock_sensor_device, SetFrequency(_, _)).Times(1);
  EXPECT_CALL(mock_sensor_device, SetChannelsEnabled(_, _, _))
      .Times(1)
      .WillOnce(WithArg<2>(
          [](base::OnceCallback<void(const std::vector<int32_t>&)> cb) {
            std::move(cb).Run({});
          }));
  EXPECT_CALL(mock_sensor_device, StartReadingSamples(_))
      .Times(1)
      .WillOnce(WithArg<0>(
          [](mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver>
                 observer) {
            mojo::Remote<cros::mojom::SensorDeviceSamplesObserver> remote;
            remote.Bind(std::move(observer));
            auto samples_in_total = kNumberFirstReadsDiscarded + kTestSamples;
            for (int i = 0; i < samples_in_total; ++i) {
              remote->OnSampleUpdated({{0, 1}, {1, 1}, {2, 1}});
            }
            remote.FlushForTesting();
          }));
  EXPECT_CALL(mock_sensor_device, StopReadingSamples()).Times(1);

  EXPECT_CALL(*mock_mojo_service, GetSensorDevice(_))
      .Times(6)
      .WillRepeatedly(Return(&mock_sensor_device));

  // std::vector<FakeDeviceArgs> device_args = {
  //     {.location = "test_location", .name = "test_name"}};

  auto iio_context = CreateFakeIioContext({FakeDeviceArgs()});
  auto iio_ec_sensor_utils = CreateIioEcSensorUtils(
      mock_mojo_service, std::move(iio_context), "base", "cros-ec-accel");

  EXPECT_TRUE(iio_ec_sensor_utils->IsInitialized());
  EXPECT_TRUE(iio_ec_sensor_utils->GetAvgData(base::DoNothing(), kTestChannels,
                                              kTestSamples));
}

}  // namespace rmad
