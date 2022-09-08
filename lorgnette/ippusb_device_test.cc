// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/ippusb_device.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace lorgnette {

TEST(IppUsbDeviceLookup, NoBackendForNonIppUsb) {
  std::optional<std::string> backend =
      BackendForDevice("notippusb:device_string");
  EXPECT_FALSE(backend.has_value());
}

TEST(IppUsbDeviceLookup, NoBackendForBadFormat) {
  std::optional<std::string> backend =
      BackendForDevice("ippusb:not_an_escl_string");
  EXPECT_FALSE(backend.has_value());
}

}  // namespace lorgnette
