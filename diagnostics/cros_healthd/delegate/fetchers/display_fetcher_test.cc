// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/display_fetcher.h"

#include <memory>
#include <utility>

#include <base/strings/string_split.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/delegate/utils/fake_display_util.h"
#include "diagnostics/cros_healthd/delegate/utils/mock_display_util_factory.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::Return;
using ::testing::SizeIs;

TEST(DisplayFetcherTest, ErrorIfFailedToCreateDisplayUtil) {
  MockDisplayUtilFactory display_util_factory;
  EXPECT_CALL(display_util_factory, Create()).WillOnce(Return(nullptr));

  mojom::DisplayResultPtr result = GetDisplayInfo(&display_util_factory);
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  const auto& error = result->get_error();
  ASSERT_TRUE(error);
  EXPECT_EQ(error->msg, "Failed to create DisplayUtil object.");
  EXPECT_EQ(error->type, mojom::ErrorType::kSystemUtilityError);
}

TEST(DisplayFetcherTest, EmbeddedDisplayInfo) {
  mojom::EmbeddedDisplayInfoPtr fake_info = mojom::EmbeddedDisplayInfo::New();
  fake_info->display_height = mojom::NullableUint32::New(100);
  fake_info->display_width = mojom::NullableUint32::New(200);
  auto display_util = std::make_unique<FakeDisplayUtil>();
  display_util->SetEmbeddedDisplayInfo(fake_info.Clone());

  MockDisplayUtilFactory display_util_factory;
  EXPECT_CALL(display_util_factory, Create())
      .WillOnce(Return(std::move(display_util)));

  mojom::DisplayResultPtr result = GetDisplayInfo(&display_util_factory);
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_display_info());
  const auto& display_info = result->get_display_info();
  ASSERT_TRUE(display_info);
  EXPECT_EQ(display_info->embedded_display, fake_info);
}

TEST(DisplayFetcherTest, NoExternalDisplay) {
  auto display_util = std::make_unique<FakeDisplayUtil>();
  display_util->SetExternalDisplayConnectorIDs({});

  MockDisplayUtilFactory display_util_factory;
  EXPECT_CALL(display_util_factory, Create())
      .WillOnce(Return(std::move(display_util)));

  mojom::DisplayResultPtr result = GetDisplayInfo(&display_util_factory);
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_display_info());
  const auto& display_info = result->get_display_info();
  ASSERT_TRUE(display_info);
  EXPECT_EQ(display_info->external_displays, std::nullopt);
}

TEST(DisplayFetcherTest, HasExternalDisplay) {
  mojom::ExternalDisplayInfoPtr fake_info_0 = mojom::ExternalDisplayInfo::New();
  fake_info_0->display_height = mojom::NullableUint32::New(100);
  fake_info_0->display_width = mojom::NullableUint32::New(200);
  mojom::ExternalDisplayInfoPtr fake_info_1 = mojom::ExternalDisplayInfo::New();
  fake_info_1->display_height = mojom::NullableUint32::New(300);
  fake_info_1->display_width = mojom::NullableUint32::New(400);

  auto display_util = std::make_unique<FakeDisplayUtil>();
  display_util->SetExternalDisplayConnectorIDs({0, 1});
  display_util->SetExternalDisplayInfo(0, fake_info_0->Clone());
  display_util->SetExternalDisplayInfo(1, fake_info_1->Clone());

  MockDisplayUtilFactory display_util_factory;
  EXPECT_CALL(display_util_factory, Create())
      .WillOnce(Return(std::move(display_util)));

  mojom::DisplayResultPtr result = GetDisplayInfo(&display_util_factory);
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_display_info());
  const auto& display_info = result->get_display_info();
  ASSERT_TRUE(display_info);
  ASSERT_TRUE(display_info->external_displays.has_value());
  const auto& external_displays = display_info->external_displays.value();
  ASSERT_THAT(external_displays, SizeIs(2));
  EXPECT_EQ(external_displays[0], fake_info_0->Clone());
  EXPECT_EQ(external_displays[1], fake_info_1->Clone());
}

}  // namespace
}  // namespace diagnostics
