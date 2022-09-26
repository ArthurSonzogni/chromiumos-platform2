// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_metrics_utils.h"

#include <gtest/gtest.h>

namespace shill {

TEST(WiFiMetricsUtilsTest, CanNotReportDisallowedOUI) {
  // It is possible in theory that at some point the hardcoded OUI 12:34:56
  // will be in the OUI allowlist. If that ever happens, it would be fine to
  // change this test OUI with another one that is not expected to be included
  // in the list of acceptable OUIs.
  EXPECT_FALSE(WiFiMetricsUtils::CanReportOUI(0x123456));
}

TEST(WiFiMetricsUtilsTest, CanReportAllowlistedOUI) {
  EXPECT_TRUE(WiFiMetricsUtils::CanReportOUI(
      WiFiMetricsUtils::AllowlistedOUIForTesting()));
}

}  // namespace shill
