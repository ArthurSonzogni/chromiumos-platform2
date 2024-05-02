// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensor_detail.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <iioservice/mojo/sensor.mojom.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr int32_t kTestSensorId = 123;

TEST(SensorDetailTest, UnsupportedSensor) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::LIGHT});
  EXPECT_EQ(sensor, nullptr);
}

TEST(SensorDetailTest, EmptySensorTypes) {
  EXPECT_EQ(SensorDetail::Create(kTestSensorId, /*types=*/{}), nullptr);
}

TEST(SensorDetailTest, GetRequiredChannelsIndicesSuccess) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  auto expected_indices = std::vector<int32_t>{0, 1, 2, 3};
  EXPECT_EQ(indices, expected_indices);
}

TEST(SensorDetailTest, GetRequiredChannelsIndicesError) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  // accel_x is missing.
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_y", "accel_z"});
  EXPECT_EQ(indices, std::nullopt);
}

TEST(SensorDetailTest, UpdateChannelSampleAndNoError) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  ASSERT_TRUE(indices.has_value());

  sensor->UpdateChannelSample(0, 21);
  sensor->UpdateChannelSample(0, 5);
  sensor->UpdateChannelSample(1, 14624);
  sensor->UpdateChannelSample(1, 14613);
  sensor->UpdateChannelSample(2, 6373);
  sensor->UpdateChannelSample(2, 6336);
  sensor->UpdateChannelSample(3, 2389718579704);
  sensor->UpdateChannelSample(3, 2389880497684);

  EXPECT_FALSE(sensor->IsErrorOccurred());
  EXPECT_TRUE(sensor->AllChannelsChecked());
}

TEST(SensorDetailTest, NotAllChannelsChecked) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  ASSERT_TRUE(indices.has_value());

  sensor->UpdateChannelSample(0, 21);
  sensor->UpdateChannelSample(0, 5);
  sensor->UpdateChannelSample(1, 14624);
  sensor->UpdateChannelSample(1, 14613);
  sensor->UpdateChannelSample(2, 6373);
  sensor->UpdateChannelSample(3, 2389718579704);

  EXPECT_FALSE(sensor->IsErrorOccurred());
  EXPECT_FALSE(sensor->AllChannelsChecked());
}

TEST(SensorDetailTest, IsErrorOccurredNoChannels) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  EXPECT_TRUE(sensor->IsErrorOccurred());
}

TEST(SensorDetailTest, IsErrorOccurredNoLastReadingSample) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  ASSERT_TRUE(indices.has_value());

  // No sample on channel 0 and 1.
  sensor->UpdateChannelSample(2, 6373);
  sensor->UpdateChannelSample(3, 2389718579704);
  EXPECT_TRUE(sensor->IsErrorOccurred());
}

TEST(SensorDetailTest, ToDict) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  ASSERT_TRUE(indices.has_value());

  base::Value::Dict expected_value;
  expected_value.Set("id", kTestSensorId);
  base::Value::List out_types;
  out_types.Append("Accel");
  expected_value.Set("types", std::move(out_types));
  base::Value::List out_channels;
  out_channels.Append(cros::mojom::kTimestampChannel);
  out_channels.Append("accel_x");
  out_channels.Append("accel_y");
  out_channels.Append("accel_z");
  expected_value.Set("channels", std::move(out_channels));

  EXPECT_EQ(sensor->ToDict(), expected_value);
}

TEST(SensorDetailTest, ToMojo) {
  auto sensor =
      SensorDetail::Create(kTestSensorId, {cros::mojom::DeviceType::ACCEL});
  ASSERT_TRUE(sensor);
  auto indices = sensor->CheckRequiredChannelsAndGetIndices(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  ASSERT_TRUE(indices.has_value());

  auto expected_value = mojom::SensitiveSensorInfo::New();
  expected_value->id = kTestSensorId;
  expected_value->types.push_back(mojom::SensitiveSensorInfo::Type::kAccel);
  expected_value->channels = {cros::mojom::kTimestampChannel, "accel_x",
                              "accel_y", "accel_z"};
  EXPECT_EQ(sensor->ToMojo(), expected_value);
}

}  // namespace
}  // namespace diagnostics
