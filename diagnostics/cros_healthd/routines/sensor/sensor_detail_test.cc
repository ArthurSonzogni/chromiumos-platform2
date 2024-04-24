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

namespace diagnostics {
namespace {

TEST(SensorDetailTest, UpdateChannelSampleAndNoError) {
  SensorDetail sensor;
  sensor.types = {cros::mojom::DeviceType::ACCEL};
  sensor.channels = {cros::mojom::kTimestampChannel, "accel_x", "accel_y",
                     "accel_z"};
  sensor.checking_channel_sample[0] = std::nullopt;
  sensor.checking_channel_sample[1] = std::nullopt;
  sensor.checking_channel_sample[2] = std::nullopt;
  sensor.checking_channel_sample[3] = std::nullopt;

  sensor.UpdateChannelSample(0, 21);
  sensor.UpdateChannelSample(0, 5);
  sensor.UpdateChannelSample(1, 14624);
  sensor.UpdateChannelSample(1, 14613);
  sensor.UpdateChannelSample(2, 6373);
  sensor.UpdateChannelSample(2, 6336);
  sensor.UpdateChannelSample(3, 2389718579704);
  sensor.UpdateChannelSample(3, 2389880497684);

  EXPECT_FALSE(sensor.IsErrorOccurred());
}

TEST(SensorDetailTest, IsErrorOccurredNoChannels) {
  SensorDetail sensor;
  sensor.types = {cros::mojom::DeviceType::ACCEL};
  sensor.channels = {};
  EXPECT_TRUE(sensor.IsErrorOccurred());
}

TEST(SensorDetailTest, IsErrorOccurredNoLastReadingSample) {
  SensorDetail sensor;
  sensor.types = {cros::mojom::DeviceType::ACCEL};
  sensor.channels = {cros::mojom::kTimestampChannel, "accel_x", "accel_y",
                     "accel_z"};
  sensor.checking_channel_sample[0] = std::nullopt;
  EXPECT_TRUE(sensor.IsErrorOccurred());
}

TEST(SensorDetailTest, GetDetailValue) {
  SensorDetail sensor;
  sensor.types = {cros::mojom::DeviceType::ACCEL};
  sensor.channels = {cros::mojom::kTimestampChannel, "accel_x", "accel_y",
                     "accel_z"};

  const int32_t test_id = 123;
  auto got_value = sensor.GetDetailValue(test_id);

  base::Value::Dict expected_value;
  expected_value.Set("id", test_id);
  base::Value::List out_types;
  out_types.Append("Accel");
  expected_value.Set("types", std::move(out_types));
  base::Value::List out_channels;
  out_channels.Append(cros::mojom::kTimestampChannel);
  out_channels.Append("accel_x");
  out_channels.Append("accel_y");
  out_channels.Append("accel_z");
  expected_value.Set("channels", std::move(out_channels));

  EXPECT_EQ(got_value, expected_value);
}

}  // namespace
}  // namespace diagnostics
