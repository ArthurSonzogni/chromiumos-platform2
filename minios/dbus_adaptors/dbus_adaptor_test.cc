// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/dbus_adaptors/dbus_adaptor.h"
#include "minios/mock_minios.h"

namespace minios {

class DBusServiceTest : public testing::Test {
 public:
  DBusServiceTest()
      : mock_mini_os_(std::make_shared<MockMiniOs>()),
        dbus_service_(std::make_unique<DBusService>(mock_mini_os_)) {}

 protected:
  std::shared_ptr<MockMiniOs> mock_mini_os_;
  std::unique_ptr<DBusService> dbus_service_;

 private:
  DBusServiceTest(const DBusServiceTest&) = delete;
  DBusServiceTest& operator=(const DBusServiceTest&) = delete;
};

TEST_F(DBusServiceTest, GetState) {
  EXPECT_CALL(*mock_mini_os_, GetState)
      .WillOnce(
          testing::Invoke([](State* state_out, brillo::ErrorPtr*) -> bool {
            EXPECT_EQ(state_out->state(), State::IDLE);
            return true;
          }));
  State mini_os_state;
  EXPECT_TRUE(dbus_service_->GetState(nullptr, &mini_os_state));
}

}  // namespace minios
