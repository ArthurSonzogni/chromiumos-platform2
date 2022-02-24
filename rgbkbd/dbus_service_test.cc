// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/dbus/dbus_object_test_helpers.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/rgbkbd/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rgbkbd/dbus_service.h"

using brillo::dbus_utils::AsyncEventSequencer;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

namespace rgbkbd {

class DBusServiceTest : public testing::Test {
 public:
  DBusServiceTest() = default;
  ~DBusServiceTest() override = default;
};

TEST_F(DBusServiceTest, DoNothing) {
  // TODO(michaelcheco): Remove stub test.
}

}  // namespace rgbkbd
