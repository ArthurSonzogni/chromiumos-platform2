// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

class DisplayFetcherTest : public ::testing::Test {
 protected:
  DisplayFetcherTest() = default;

  mojo_ipc::DisplayResultPtr FetchDisplayInfo() {
    base::test::TestFuture<mojo_ipc::DisplayResultPtr> future;
    display_fetcher_.FetchDisplayInfo(future.GetCallback());
    return future.Take();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  DisplayFetcher display_fetcher_{&mock_context_};
};

TEST_F(DisplayFetcherTest, EmbeddedDisplayInfo) {
  auto display_result = FetchDisplayInfo();

  ASSERT_TRUE(display_result->is_display_info());
  const auto& display_info = display_result->get_display_info();

  // Answer can be found in fake_libdrm_util.cc.
  const auto& edp_info = display_info->edp_info;
  EXPECT_TRUE(edp_info->privacy_screen_supported);
  EXPECT_FALSE(edp_info->privacy_screen_enabled);
  EXPECT_EQ(edp_info->display_width->value, 290);
  EXPECT_EQ(edp_info->display_height->value, 190);
  EXPECT_EQ(edp_info->resolution_horizontal->value, 1920);
  EXPECT_EQ(edp_info->resolution_vertical->value, 1080);
  EXPECT_NEAR(edp_info->refresh_rate->value, 60.00, 1e-6);
  EXPECT_EQ(edp_info->manufacturer, "AUO");
  EXPECT_EQ(edp_info->model_id->value, 0x323D);
  EXPECT_FALSE(edp_info->serial_number);
  EXPECT_EQ(edp_info->manufacture_year->value, 2018);
  EXPECT_EQ(edp_info->manufacture_week->value, 20);
  EXPECT_EQ(edp_info->edid_version, "1.4");
  EXPECT_EQ(edp_info->input_type, mojo_ipc::DisplayInputType::kDigital);
  EXPECT_FALSE(edp_info->display_name.has_value());

  const auto& dp_infos = display_info->dp_infos;
  EXPECT_EQ(dp_infos->size(), 2);
  for (const auto& dp_info : *dp_infos) {
    EXPECT_EQ(dp_info->display_width->value, 600);
    EXPECT_EQ(dp_info->display_height->value, 340);
    EXPECT_EQ(dp_info->resolution_horizontal->value, 2560);
    EXPECT_EQ(dp_info->resolution_vertical->value, 1440);
    EXPECT_NEAR(dp_info->refresh_rate->value, 120.00, 1e-6);
    EXPECT_EQ(dp_info->manufacturer, "DEL");
    EXPECT_EQ(dp_info->model_id->value, 0x4231);
    EXPECT_EQ(dp_info->serial_number->value, 1162368076);
    EXPECT_EQ(dp_info->manufacture_year->value, 2022);
    EXPECT_EQ(dp_info->manufacture_week->value, 3);
    EXPECT_EQ(dp_info->edid_version, "1.3");
    EXPECT_EQ(dp_info->input_type, mojo_ipc::DisplayInputType::kAnalog);
    EXPECT_EQ(dp_info->display_name, "DELL U2722DE");
  }
}

}  // namespace
}  // namespace diagnostics
