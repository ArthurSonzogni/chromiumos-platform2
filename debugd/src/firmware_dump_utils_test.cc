// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/firmware_dump_utils.h"

#include <stdint.h>

#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/mock_log.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object_test_helpers.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/files/file_util.h>
#include <chromeos/dbus/debugd/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/src/debugd_dbus_adaptor.h"
#include "debugd/src/path_utils.h"

using ::testing::_;
using ::testing::AtLeast;

namespace {

// Builds a mocked D-Bus response for async methods. Regarding the failure and
// success cases, below are the usages for testing:
// 1. The failure cases (|ReplyWithError|) are well-mocked and can be set as
// expectations directly.
// 2. The success cases (|Return| with no error) are not mocked, but |Return| is
// overridden to store the output in |response| so that the caller can set
// expectations on |response| later.
template <typename T>
std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<T>>
BuildMockDBusResponse(T* response) {
  auto dbus_response =
      std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<T>>(
          /*method_call = nullptr*/);
  dbus_response->set_return_callback(
      base::BindOnce([](T* response_out,
                        const T& response_in) { *response_out = response_in; },
                     response));
  return dbus_response;
}

}  // namespace

namespace debugd {
class FirmwareDumpUtilsTest : public ::testing::Test {
 protected:
  void CreateDirectory(const base::FilePath& dir) {
    base::File::Error error;
    ASSERT_TRUE(base::CreateDirectoryAndGetError(dir, &error))
        << base::File::ErrorToString(error);
  }

  // Create the debugfs file and all its parent directories.
  void CreateDebugfsFile(std::string_view dumper) {
    const base::FilePath debugfs_path = path_utils::GetFilePath(dumper);
    ASSERT_TRUE(brillo::DeleteFile(debugfs_path));
    CreateDirectory(debugfs_path.DirName());
    ASSERT_TRUE(base::WriteFile(debugfs_path, ""));
  }

 private:
  void SetUp() override {
    CHECK(tmp_dir_.CreateUniqueTempDir()) << "Fail to create temp directory";
    path_utils::testing::SetPrefixForTesting(tmp_dir_.GetPath());
  }

  void TearDown() override {
    // Reset the prefix after test.
    path_utils::testing::SetPrefixForTesting(base::FilePath());
  }

  base::ScopedTempDir tmp_dir_;
};

TEST_F(FirmwareDumpUtilsTest, ConvertFirmwareDumpType) {
  const auto fwdump_type_all = ConvertFirmwareDumpType(0);
  EXPECT_TRUE(fwdump_type_all.has_value());
  EXPECT_EQ(FirmwareDumpType::ALL, fwdump_type_all.value());

  const auto fwdump_type_wifi = ConvertFirmwareDumpType(1);
  EXPECT_TRUE(fwdump_type_wifi.has_value());
  EXPECT_EQ(FirmwareDumpType::WIFI, fwdump_type_wifi.value());

  const auto fwdump_type_invalid = ConvertFirmwareDumpType(UINT32_MAX);
  EXPECT_FALSE(fwdump_type_invalid.has_value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathSuccess) {
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, FirmwareDumpOperation::GenerateFirmwareDump);
  EXPECT_TRUE(dumper_path.has_value());
  EXPECT_EQ(path_utils::GetFilePath(
                "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect")
                .value(),
            dumper_path->value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathNoBaseDirectoryForType) {
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  auto dumper_path =
      FindDebugfsPath(static_cast<FirmwareDumpType>(UINT32_MAX),
                      FirmwareDumpOperation::GenerateFirmwareDump);
  log.StopCapturingLogs();
  EXPECT_FALSE(dumper_path.has_value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathNoBaseDirectoryOnFilesystem) {
  CreateDebugfsFile("/sys/kernel/debug/path/to/fake/file");
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, FirmwareDumpOperation::GenerateFirmwareDump);
  log.StopCapturingLogs();
  EXPECT_FALSE(dumper_path.has_value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathNoFirmwareDumpOperationFile) {
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, static_cast<FirmwareDumpOperation>(UINT32_MAX));
  log.StopCapturingLogs();
  EXPECT_FALSE(dumper_path.has_value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathEmptyBaseDirectory) {
  CreateDirectory(path_utils::GetFilePath("/sys/kernel/debug/iwlwifi"));
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, FirmwareDumpOperation::GenerateFirmwareDump);
  log.StopCapturingLogs();
  EXPECT_FALSE(dumper_path.has_value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathMultipleSubdirectory) {
  // Normally there is only one PCIe device for a particular WiFi driver ("/sys/
  // kernel/debug/iwlwifi/" in the following test case) and one sub-directory
  // ("0000:00:14.3/iwlmvm/") for a collection of unique debugfs files.
  // |FindDebugfsPath| is based on this assumption and exits the sub-directory
  // search loop once the targeted debugfs file is found. This test case makes
  // sure |FindDebugfsPath| can find the targeted debugfs file when there are
  // multiple sub-directories, but doesn't cover the case where there are more
  // than one debugfs files with the same name under different sub-directories.
  // If this assumption no longer holds for other device driver, modifications
  // on the search strategy in |FindDebugfsPath| are required.
  CreateDebugfsFile("/sys/kernel/debug/iwlwifi/0000:00:14.2/fake/file");
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, FirmwareDumpOperation::GenerateFirmwareDump);
  EXPECT_TRUE(dumper_path.has_value());
  EXPECT_EQ(path_utils::GetFilePath(
                "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect")
                .value(),
            dumper_path->value());
}

TEST_F(FirmwareDumpUtilsTest, FindDebugfsPathNoDumperFile) {
  CreateDebugfsFile("/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fake_file");
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  auto dumper_path = FindDebugfsPath(
      FirmwareDumpType::WIFI, FirmwareDumpOperation::GenerateFirmwareDump);
  log.StopCapturingLogs();
  EXPECT_FALSE(dumper_path.has_value());
}

TEST_F(FirmwareDumpUtilsTest, WriteToDebugfsSuccess) {
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  EXPECT_TRUE(WriteToDebugfs(FirmwareDumpType::WIFI,
                             FirmwareDumpOperation::GenerateFirmwareDump, "1"));
  std::string content;
  EXPECT_TRUE(base::ReadFileToString(
      path_utils::GetFilePath(
          "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect"),
      &content));
  EXPECT_EQ("1", content);
}

TEST_F(FirmwareDumpUtilsTest, WriteToDebugfsFailToFindPath) {
  CreateDebugfsFile("/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fake_file");
  EXPECT_FALSE(WriteToDebugfs(FirmwareDumpType::WIFI,
                              FirmwareDumpOperation::GenerateFirmwareDump,
                              "1"));
}

TEST_F(FirmwareDumpUtilsTest, WriteToDebugfsFailToWrite) {
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  // Remove the write permission to the debugfs file to mimic write failure.
  ASSERT_TRUE(SetPosixFilePermissions(
      path_utils::GetFilePath(
          "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect"),
      0400))
      << "Fail to remove write permission to the debugfs file";
  base::test::MockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log, Log(::logging::LOGGING_ERROR, _, _, _, _)).Times(AtLeast(1));
  EXPECT_FALSE(WriteToDebugfs(FirmwareDumpType::WIFI,
                              FirmwareDumpOperation::GenerateFirmwareDump,
                              "1"));
  log.StopCapturingLogs();
}

TEST_F(FirmwareDumpUtilsTest, GenerateFirmwareDumpForAll) {
  bool response = false;
  // |ALL| is reserved and we don't support for now.
  auto mocked_method_response = BuildMockDBusResponse(&response);
  EXPECT_CALL(
      *mocked_method_response,
      ReplyWithError(_, _, _,
                     "Firmware dump operation is not supported for type: 0"));
  GenerateFirmwareDumpHelper(std::move(mocked_method_response),
                             FirmwareDumpType::ALL);
  EXPECT_FALSE(response);
}

TEST_F(FirmwareDumpUtilsTest, GenerateFirmwareDumpForWiFiSuccess) {
  bool response = false;
  auto mocked_method_response = BuildMockDBusResponse(&response);
  CreateDebugfsFile(
      "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect");
  EXPECT_CALL(*mocked_method_response, ReplyWithError(_, _, _, _)).Times(0);
  GenerateFirmwareDumpHelper(std::move(mocked_method_response),
                             FirmwareDumpType::WIFI);
  EXPECT_TRUE(response);
  std::string content;
  EXPECT_TRUE(base::ReadFileToString(
      path_utils::GetFilePath(
          "/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fw_dbg_collect"),
      &content));
  EXPECT_EQ("1", content);
}

TEST_F(FirmwareDumpUtilsTest, GenerateFirmwareDumpForWiFiError) {
  bool response = false;
  auto mocked_method_response = BuildMockDBusResponse(&response);
  CreateDebugfsFile("/sys/kernel/debug/iwlwifi/0000:00:14.3/iwlmvm/fake_file");
  EXPECT_CALL(*mocked_method_response,
              ReplyWithError(_, _, _, "Failed to write to debugfs"));
  GenerateFirmwareDumpHelper(std::move(mocked_method_response),
                             FirmwareDumpType::WIFI);
  EXPECT_FALSE(response);
}

}  // namespace debugd
