// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "machine-id-regen/machine_id_regen.h"

#include <base/files/scoped_temp_dir.h>
#include <base/time/time.h>
#include <brillo/file_utils.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "machine-id-regen/timestamp.h"

using ::testing::_;
using ::testing::Return;

namespace machineidregen {

namespace {

class TimestampTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    timestamp_ = new Timestamp(temp_dir_.GetPath().Append("tmp_timestamp"));
  }

  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

  base::ScopedTempDir temp_dir_;

 protected:
  Timestamp* timestamp_;
};

}  // namespace

// Test get_last_update.
TEST_F(TimestampTest, GetLastUpdate) {
  // Check timestamp file path.
  EXPECT_EQ(temp_dir_.GetPath().Append("tmp_timestamp"), timestamp_->GetPath());

  // Test no timestamp file.
  std::optional<base::TimeDelta> ret = timestamp_->get_last_update();
  EXPECT_EQ(false, ret.has_value());

  // Test updating timestamp.
  std::string ten_secs = "10";
  brillo::WriteStringToFile(timestamp_->GetPath(), ten_secs);
  EXPECT_EQ(base::Seconds(10), timestamp_->get_last_update());

  // Test long timestamp.
  std::string long_string;
  long_string.resize(1023, '0');
  long_string += "1";

  brillo::WriteStringToFile(timestamp_->GetPath(), long_string);
  ret = timestamp_->get_last_update();
  EXPECT_EQ(true, ret.has_value());
  EXPECT_EQ(base::Seconds(1), ret.value());

  // Test too long timestamp.
  long_string += "1";

  brillo::WriteStringToFile(timestamp_->GetPath(), long_string);
  ret = timestamp_->get_last_update();
  EXPECT_EQ(false, ret.has_value());
}

// Test update.
TEST_F(TimestampTest, Update) {
  // Check Update.
  EXPECT_EQ(true, timestamp_->update(base::Seconds(3)));
  EXPECT_EQ(base::Seconds(3), timestamp_->get_last_update());

  EXPECT_EQ(true, timestamp_->update(base::Seconds(57)));
  EXPECT_EQ(base::Seconds(57), timestamp_->get_last_update());

  EXPECT_EQ(true, timestamp_->update(base::Seconds(10301)));
  EXPECT_EQ(base::Seconds(10301), timestamp_->get_last_update());
}

namespace {

constexpr char kAvahiServiceName[] = "org.freedesktop.Avahi";
constexpr char kTestError[] = "test error";
constexpr char kTestMachineId[] = "1234";

class SendMachineIdToAvahiTest : public ::testing::Test {
 public:
  void SetUp() override {
    dbus::Bus::Options opts;
    opts.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options());
    mock_avahi_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        mock_bus_.get(), kAvahiServiceName, dbus::ObjectPath("/"));
  }

  void TearDown() override {
    mock_avahi_proxy_ = nullptr;
    mock_bus_ = nullptr;
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_avahi_proxy_;
};

}  // namespace

// Test send_machine_id_to_avahi.
TEST_F(SendMachineIdToAvahiTest, ApiTest) {
  EXPECT_CALL(*mock_bus_.get(),
              GetObjectProxy(kAvahiServiceName, dbus::ObjectPath("/")))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(false, send_machine_id_to_avahi(mock_bus_, "1234"));

  EXPECT_CALL(*mock_bus_.get(),
              GetObjectProxy(kAvahiServiceName, dbus::ObjectPath("/")))
      .WillRepeatedly(Return(mock_avahi_proxy_.get()));
  EXPECT_CALL(*mock_avahi_proxy_.get(), CallMethodAndBlock(_, _))
      .WillOnce(Return(base::unexpected(dbus::Error(kTestError, kTestError))));
  EXPECT_EQ(false, send_machine_id_to_avahi(mock_bus_, kTestMachineId));

  EXPECT_CALL(*mock_avahi_proxy_.get(), CallMethodAndBlock(_, _))
      .WillOnce(Return(dbus::Response::CreateEmpty()));
  EXPECT_EQ(true, send_machine_id_to_avahi(mock_bus_, kTestMachineId));
}

}  // namespace machineidregen
