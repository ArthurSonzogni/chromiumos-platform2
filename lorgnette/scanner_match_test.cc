// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/scanner_match.h"

#include <string>

#include <gtest/gtest.h>
#include <base/containers/flat_set.h>

namespace lorgnette {
namespace {

const char* kScannerNameVidpid = "pixma:12344321_AF123";
const char* kScannerNameBusdev = "epsonds:libusb:001:002";

TEST(ScannerMatchTest, ExtractVidPidOk) {
  auto vid_pid_result = ExtractIdentifiersFromDeviceName(
      kScannerNameVidpid,
      "pixma:([0-9a-fA-F]{4})([0-9a-fA-F]{4})_[0-9a-fA-F]*");

  EXPECT_TRUE(vid_pid_result.has_value());
  EXPECT_EQ(vid_pid_result.value().first, "1234");
  EXPECT_EQ(vid_pid_result.value().second, "4321");
}

TEST(ScannerMatchTest, ExtractBusDevOk) {
  auto bus_dev_result = ExtractIdentifiersFromDeviceName(
      kScannerNameBusdev, "epson(?:2|ds)?:libusb:([0-9]{3}):([0-9]{3})");

  EXPECT_TRUE(bus_dev_result.has_value());
  EXPECT_EQ(bus_dev_result.value().first, "001");
  EXPECT_EQ(bus_dev_result.value().second, "002");
}

TEST(ScannerMatchTest, NoMatchFound) {
  auto vid_pid_result = ExtractIdentifiersFromDeviceName(
      "pixma:123421_AB3",
      "pixma:([0-9a-fA-F]{4})([0-9a-fA-F]{4})_[0-9a-fA-F]*");

  EXPECT_FALSE(vid_pid_result.has_value());
}

TEST(ScannerMatchTest, DuplicateVidPidOk) {
  base::flat_set<std::string> seen_vidpids;
  base::flat_set<std::string> seen_busdevs;
  seen_vidpids.insert("1234:4321");

  EXPECT_TRUE(
      DuplicateScannerExists(kScannerNameVidpid, seen_vidpids, seen_busdevs));
}

TEST(ScannerMatchTest, DuplicateBusDevOk) {
  base::flat_set<std::string> seen_vidpids;
  base::flat_set<std::string> seen_busdevs;
  seen_busdevs.insert("001:002");

  EXPECT_TRUE(
      DuplicateScannerExists(kScannerNameBusdev, seen_vidpids, seen_busdevs));
}

TEST(ScannerMatchTest, NoDuplicatesFound) {
  base::flat_set<std::string> seen_vidpids;
  base::flat_set<std::string> seen_busdevs;
  seen_vidpids.insert("5678:8765");
  seen_busdevs.insert("003:004");

  EXPECT_FALSE(
      DuplicateScannerExists(kScannerNameVidpid, seen_vidpids, seen_busdevs));
  EXPECT_FALSE(
      DuplicateScannerExists(kScannerNameBusdev, seen_vidpids, seen_busdevs));
}

TEST(ScannerMatchTest, EpsonConnections) {
  ScannerInfo info;
  info.set_name("epson2:net:1.2.3.4");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_NETWORK);

  info.set_name("epsonds:net:1.2.3.4");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_NETWORK);

  info.set_name("epson2:libusb:001:002");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_USB);

  info.set_name("epsonds:libusb:001:002");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_USB);
}

TEST(ScannerMatchTest, PixmaConnections) {
  ScannerInfo info;
  info.set_name("pixma:MF2600_1.2.3.4");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_NETWORK);

  info.set_name("pixma:04A91234_ABC123");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_USB);
}

TEST(ScannerMatchTest, OtherConnections) {
  ScannerInfo info;
  info.set_name("ippusb:escl:therest");
  EXPECT_EQ(ConnectionTypeForScanner(info), CONNECTION_USB);
}

}  // namespace
}  // namespace lorgnette
