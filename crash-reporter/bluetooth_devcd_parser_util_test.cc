// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/bluetooth_devcd_parser_util.h"

#include <vector>

#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/strings/string_utils.h>

#include <gtest/gtest.h>

#include "crash-reporter/udev_bluetooth_util.h"
#include "crash-reporter/util.h"

namespace {

constexpr char kMetaHeader[] = "Bluetooth devcoredump";

}  // namespace

class BluetoothDevcdParserUtilTest : public ::testing::Test {
 protected:
  void CreateDumpFile(const std::vector<std::string>& meta_data,
                      const std::vector<uint8_t>& data = {}) {
    std::string meta_data_str = brillo::string_utils::Join("\n", meta_data);
    std::string data_header = "\n--- Start dump ---\n";

    // Clear previous test files, if any
    ASSERT_TRUE(base::DeleteFile(dump_path_));
    ASSERT_TRUE(base::DeleteFile(target_path_));
    ASSERT_TRUE(base::DeleteFile(data_path_));

    base::File file(dump_path_,
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);

    ASSERT_TRUE(file.IsValid());

    ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(
        base::as_bytes(base::make_span(meta_data_str))));

    if (!data.empty()) {
      ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(
          base::as_bytes(base::make_span(data_header))));
      ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::make_span(data)));
    }
  }

  void VerifyProcessedDump(const std::vector<std::string>& want_lines) {
    base::File file(target_path_,
                    base::File::FLAG_OPEN | base::File::FLAG_READ);
    std::string line;
    for (const auto& want : want_lines) {
      ASSERT_GT(util::GetNextLine(file, line), 0);
      EXPECT_EQ(line, want);
    }

    // Make sure there are no more lines
    ASSERT_EQ(util::GetNextLine(file, line), 0);
  }

  base::FilePath output_dir_;
  base::FilePath dump_path_;
  base::FilePath target_path_;
  base::FilePath data_path_;

 private:
  void SetUp() override {
    CHECK(tmp_dir_.CreateUniqueTempDir());
    output_dir_ = tmp_dir_.GetPath();
    dump_path_ = output_dir_.Append("bt_firmware.devcd");
    target_path_ = output_dir_.Append("bt_firmware.txt");
    data_path_ = output_dir_.Append("bt_firmware.data");
  }

  base::ScopedTempDir tmp_dir_;
};

// Test a failure case when reading the input coredump file fails.
TEST_F(BluetoothDevcdParserUtilTest, TestInvalidPath) {
  std::string sig;

  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(
      dump_path_.ReplaceExtension("invalid"), output_dir_, true, &sig));
}

// A key-value pair in header fields is of type "<key>: <value>". Verify
// that malformed key-value pairs are not parsed and an error is returned.
TEST_F(BluetoothDevcdParserUtilTest, TestInvalidHeaderField) {
  std::string sig;

  // Test missing value in key-value pair
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State:",
  };
  CreateDumpFile(meta_data);
  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                      false, &sig));

  // Test malformed key-value pair
  meta_data = {
      kMetaHeader,
      "State 0",
  };
  CreateDumpFile(meta_data);
  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                      false, &sig));
}

// Verify that the devcodedump state with a value other than 0-4 is reported
// as is. For values between 0 through 4, it's equivalent human readable state
// string is reported.
TEST_F(BluetoothDevcdParserUtilTest, TestInvalidState) {
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State: -1",
      "Driver: TestDrv",
      "Vendor: TestVen",
      "Controller Name: TestCon",
  };
  CreateDumpFile(meta_data);

  std::string sig;
  EXPECT_TRUE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                     false, &sig));
  EXPECT_EQ(sig, "bt_firmware-TestDrv-TestVen_TestCon-00000000");

  base::File file(target_path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::string line;
  util::GetNextLine(file, line);
  EXPECT_EQ(line, "State=-1");
}

// The Driver Name, Vendor Name and Controller Name are required key-value
// pairs. Although we allow partial dumps, parsing should fail if any of
// these required keys are missing.
TEST_F(BluetoothDevcdParserUtilTest, TestMissingMetaKey) {
  std::string sig;

  // Test missing driver case
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State: 0",
      "Vendor: TestVen",
      "Controller Name: TestCon",
  };
  CreateDumpFile(meta_data);
  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                      false, &sig));

  // Test missing vendor case
  meta_data = {
      kMetaHeader,
      "State: 0",
      "Driver: TestDrv",
      "Controller Name: TestCon",
  };
  CreateDumpFile(meta_data);
  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                      false, &sig));

  // Test missing controller name case
  meta_data = {
      kMetaHeader,
      "State: 0",
      "Driver: TestDrv",
      "Vendor: TestVen",
  };
  CreateDumpFile(meta_data);
  EXPECT_FALSE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                      false, &sig));
}

// After updating the devcoredump state, the Bluetooth HCI Devcoredump
// API adds a '\0' at the end of the "State:" key-value, i.e. before the
// "Driver:" key-value pair. Verify this case.
TEST_F(BluetoothDevcdParserUtilTest, TestHeaderWithNullChar) {
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State: 2",
      std::string("\0Driver: TestDrv", 16),
      "Vendor: TestVen",
      "Controller Name: TestCon",
  };
  CreateDumpFile(meta_data);

  std::string sig;
  EXPECT_TRUE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                     false, &sig));
  EXPECT_EQ(sig, "bt_firmware-TestDrv-TestVen_TestCon-00000000");

  std::vector<std::string> want_lines = {
      "State=Devcoredump Complete", "Driver=TestDrv", "Vendor=TestVen",
      "Controller Name=TestCon",    "PC=00000000",
  };
  VerifyProcessedDump(want_lines);
}

// A bluetooth devcoredump with just a header but no vendor specific binary
// data is a valid dump. Verify that the empty dump is reported properly.
TEST_F(BluetoothDevcdParserUtilTest, TestValidEmptyDump) {
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State: 2",
      "Driver: TestDrv",
      "Vendor: TestVen",
      "Controller Name: TestCon",
  };
  CreateDumpFile(meta_data);

  std::string sig;
  EXPECT_TRUE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                     false, &sig));
  EXPECT_EQ(sig, "bt_firmware-TestDrv-TestVen_TestCon-00000000");

  std::vector<std::string> want_lines = {
      "State=Devcoredump Complete", "Driver=TestDrv", "Vendor=TestVen",
      "Controller Name=TestCon",    "PC=00000000",
  };
  VerifyProcessedDump(want_lines);
}

// For debugging purposes, vendor specific binary data is stored on a
// developer images. Verify that the header is stripped off correctly and
// the binary data is stored.
TEST_F(BluetoothDevcdParserUtilTest, TestDumpData) {
  std::vector<std::string> meta_data = {
      kMetaHeader,
      "State: 2",
      "Driver: TestDrv",
      "Vendor: TestVen",
      "Controller Name: TestCon",
  };
  std::vector<uint8_t> data = {'T', 'e', 's', 't', '\n'};
  CreateDumpFile(meta_data, data);

  std::string sig;
  EXPECT_TRUE(bluetooth_util::ParseBluetoothCoredump(dump_path_, output_dir_,
                                                     true, &sig));
  EXPECT_EQ(sig, "bt_firmware-TestDrv-TestVen_TestCon-00000000");

  std::vector<std::string> want_lines = {
      "State=Devcoredump Complete", "Driver=TestDrv", "Vendor=TestVen",
      "Controller Name=TestCon",    "PC=00000000",
  };
  VerifyProcessedDump(want_lines);

  base::File file(data_path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::string line;
  util::GetNextLine(file, line);
  EXPECT_EQ(line, "Test");
}
