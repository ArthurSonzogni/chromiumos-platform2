// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_id/client_id.h"

#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <gtest/gtest.h>

namespace client_id {

namespace {

constexpr char kLegacyClientId[] = "CloudReady-aa:aa:aa:11:22:33";
constexpr char kUuid[] = "fc71ace7-5fbb-4108-a2f5-b48a98635aeb";
constexpr char kGoodSerial[] = "good_example_serial";
constexpr char kBadSerial[] = "to be filled by o.e.m.";
constexpr char kShortSerial[] = "a";
constexpr char kRepeatedSerial[] = "aaaaaa";
constexpr char kPriorityInterfaceName[] = "eth0";
constexpr char kGoodInterfaceName[] = "wlan1";
constexpr char kBadInterfaceName[] = "arc_1";
constexpr char kGoodMacAddress[] = "aa:bb:cc:11:22:33";
constexpr char kGoodMacAddress2[] = "dd:ee:ff:44:55:66";
constexpr char kBadMacAddress[] = "00:00:00:00:00:00";
constexpr char kPciModAlias[] = "pci:0000";
constexpr char kUsbModAlias[] = "usb:0000";

}  // namespace

class ClientIdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
    client_id_generator_ = client_id::ClientIdGenerator(test_path_);
  }

  void CreateSerial(const std::string& serial) {
    base::FilePath serial_path =
        test_path_.Append("sys/devices/virtual/dmi/id");
    CHECK(base::CreateDirectory(serial_path));
    CHECK(base::WriteFile(serial_path.Append("product_serial"), serial));
  }

  void CreateInterface(const std::string& name,
                       const std::string& address,
                       const std::string& modalias) {
    base::FilePath interface_path =
        test_path_.Append("sys/class/net").Append(name);
    CHECK(base::CreateDirectory(interface_path.Append("device")));
    CHECK(base::WriteFile(interface_path.Append("address"), address));
    CHECK(base::WriteFile(interface_path.Append("device").Append("modalias"),
                          modalias));
  }

  void CreateLegacy() {
    base::FilePath legacy_path =
        test_path_.Append("mnt/stateful_partition/cloudready");
    CHECK(base::CreateDirectory(legacy_path));
    CHECK(base::WriteFile(legacy_path.Append("client_id"), kLegacyClientId));
  }

  void CreateUuid() {
    base::FilePath uuid_path = test_path_.Append("proc/sys/kernel/random");
    CHECK(base::CreateDirectory(uuid_path));
    CHECK(base::WriteFile(uuid_path.Append("uuid"), kUuid));
  }

  void DeleteClientId() {
    base::FilePath client_id_path =
        test_path_.Append("var/lib/client_id/client_id");
    CHECK(base::DeleteFile(client_id_path));
  }

  base::Optional<client_id::ClientIdGenerator> client_id_generator_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(ClientIdTest, LegacyClientId) {
  EXPECT_FALSE(client_id_generator_->TryLegacy());

  CreateLegacy();
  EXPECT_EQ(client_id_generator_->TryLegacy(), kLegacyClientId);
}

TEST_F(ClientIdTest, SerialNumber) {
  EXPECT_FALSE(client_id_generator_->TrySerial());

  // a too short serial should not be used
  CreateSerial(kShortSerial);
  EXPECT_FALSE(client_id_generator_->TrySerial());

  // a known bad serial should not be used
  CreateSerial(kBadSerial);
  EXPECT_FALSE(client_id_generator_->TrySerial());

  // a serial of only one repeated character should not be used
  CreateSerial(kRepeatedSerial);
  EXPECT_FALSE(client_id_generator_->TrySerial());

  // a good serial should be used
  CreateSerial(kGoodSerial);
  EXPECT_EQ(client_id_generator_->TrySerial(), kGoodSerial);
}

TEST_F(ClientIdTest, MacAddress) {
  EXPECT_FALSE(client_id_generator_->TryMac());

  // 00:00:00:00:00:00 mac should not be used
  CreateInterface(kPriorityInterfaceName, kBadMacAddress, kPciModAlias);
  EXPECT_FALSE(client_id_generator_->TryMac());

  // a non priority usb device should not be  used
  CreateInterface(kGoodInterfaceName, kGoodMacAddress, kUsbModAlias);
  EXPECT_FALSE(client_id_generator_->TryMac());

  // a blocked interface should not be used
  CreateInterface(kBadInterfaceName, kGoodMacAddress, kPciModAlias);
  EXPECT_FALSE(client_id_generator_->TryMac());

  // eth0 should be used
  CreateInterface(kPriorityInterfaceName, kGoodMacAddress, kPciModAlias);
  EXPECT_EQ(client_id_generator_->TryMac(), kGoodMacAddress);
}

TEST_F(ClientIdTest, Uuid) {
  EXPECT_FALSE(client_id_generator_->TryUuid());

  CreateUuid();
  EXPECT_EQ(client_id_generator_->TryUuid(), kUuid);
}

TEST_F(ClientIdTest, GenerateAndSaveClientId) {
  // no client id should be generated if there are no sources
  EXPECT_FALSE(client_id_generator_->GenerateAndSaveClientId());

  // uuid should be used for the client id
  CreateUuid();
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kUuid).value());

  // a bad interface should not be used
  DeleteClientId();
  CreateInterface(kGoodInterfaceName, kGoodMacAddress, kUsbModAlias);
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kUuid).value());

  // a good interface should take priority over uuid
  DeleteClientId();
  CreateInterface(kGoodInterfaceName, kGoodMacAddress, kPciModAlias);
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kGoodMacAddress).value());

  // a priority interface should take priority over a good interface
  DeleteClientId();
  CreateInterface(kPriorityInterfaceName, kGoodMacAddress2, kPciModAlias);
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kGoodMacAddress2).value());

  // a bad serial should not be used
  DeleteClientId();
  CreateSerial(kBadSerial);
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kGoodMacAddress2).value());

  // a good serial should take priority over mac address
  DeleteClientId();
  CreateSerial(kGoodSerial);
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(),
            client_id_generator_->AddClientIdPrefix(kGoodSerial).value());

  // legacy client_id should take priority over a good serial
  DeleteClientId();
  CreateLegacy();
  EXPECT_TRUE(client_id_generator_->GenerateAndSaveClientId());
  EXPECT_EQ(client_id_generator_->ReadClientId().value(), kLegacyClientId);
}

}  // namespace client_id
