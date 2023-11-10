// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/mock_log.h>
#include <brillo/secure_blob.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "gmock/gmock.h"
#include "minios/mock_process_manager.h"
#include "minios/utils.h"

namespace minios {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;

const brillo::SecureBlob kValidKey{"thisisa32bytestring1234567890abc"};
const brillo::SecureBlob kTestData{
    "test data to verify encryption and decryption"};

class UtilTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    file_path_ = tmp_dir_.GetPath().Append("file");
    content_ = "line1\nline2\n" + std::string(100, 'a') + "\nb";
    ASSERT_TRUE(base::WriteFile(file_path_, content_));
    ASSERT_TRUE(
        CreateDirectory(tmp_dir_.GetPath().Append("sys/firmware/vpd/ro")));
  }

 protected:
  base::ScopedTempDir tmp_dir_;
  base::FilePath file_path_;
  std::string content_;
};

TEST_F(UtilTest, ReadFileContentOffsets) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/0, /*end_offset=*/1, /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "l\n");
}

TEST_F(UtilTest, ReadFileContentOffsets2) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/0, /*end_offset=*/1, /*num_cols=*/2);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "l");
}

TEST_F(UtilTest, ReadFileContentOffsets3) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/4, /*end_offset=*/6, /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "1\n");
}

TEST_F(UtilTest, ReadFileContentOffsets4) {
  auto [success, content] = ReadFileContentWithinRange(
      file_path_, /*start_offset=*/2, /*end_offset=*/7, /*num_cols=*/2);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "ne\n1\nl");
}

TEST_F(UtilTest, ReadFileContentMissingFile) {
  auto [success, content, bytes_read] =
      ReadFileContent(base::FilePath("/a/b/foobar"), 1, 1, 1);
  EXPECT_FALSE(success);
}

TEST_F(UtilTest, ReadFileContentWrappedTextCutoff) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*offset=*/0, /*num_lines=*/3, /*num_cols=*/4);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "line\n1\nline\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentWrappedTextPerfectAlignmentColumns) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*offset=*/0, /*num_lines=*/5, /*num_cols=*/5);
  EXPECT_TRUE(success);
  // There should be no double newlining.
  EXPECT_EQ(content, "line1\nline2\naaaaa\naaaaa\naaaaa\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentWrappedTextExceedsColumnLimit) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*offset=*/0, /*num_lines=*/5, /*num_cols=*/6);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "line1\nline2\naaaaaa\naaaaaa\naaaaaa\n");
  EXPECT_LT(bytes_read, content.size());
}

TEST_F(UtilTest, ReadFileContentZeroLimits) {
  auto [success, content, bytes_read] = ReadFileContent(
      file_path_, /*offset=*/0, /*num_lines=*/0, /*num_cols=*/0);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "");
  EXPECT_EQ(bytes_read, 0);
}

TEST_F(UtilTest, ReadFileContent) {
  auto [success, content, bytes_read] =
      ReadFileContent(file_path_, /*offset=*/0, /*num_lines=*/4,
                      /*num_cols=*/200);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, content_);
  EXPECT_EQ(bytes_read, content_.size());
}

TEST_F(UtilTest, ReadFileContentStartOffset) {
  auto [success, content, bytes_read] =
      ReadFileContent(file_path_, /*offset=*/12, /*num_lines=*/3,
                      /*num_cols=*/1);
  EXPECT_TRUE(success);
  EXPECT_EQ(content, "a\na\na\n");
  EXPECT_EQ(bytes_read, 3);
}

TEST_F(UtilTest, GetKeyboardLayoutFailure) {
  auto mock_process_manager_ = std::make_shared<MockProcessManager>();
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(""), testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(mock_process_manager_), "us");

  // Badly formatted.
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>("xkbeng:::"),
                               testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(mock_process_manager_), "us");

  // Failed.
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::Return(false)));
  EXPECT_EQ(GetKeyboardLayout(mock_process_manager_), "us");
}

TEST_F(UtilTest, GetKeyboardLayout) {
  auto mock_process_manager_ = std::make_shared<MockProcessManager>();
  EXPECT_CALL(*mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>("xkb:en::eng"),
                               testing::Return(true)));
  EXPECT_EQ(GetKeyboardLayout(mock_process_manager_), "en");
}

TEST(UtilsTest, AlertLogTagCreationTest) {
  auto category = "test_category";
  auto default_component = "CoreServicesAlert";
  EXPECT_EQ(base::StringPrintf("[%s<%s>] ", default_component, category),
            AlertLogTag(category));
}

TEST(UtilsTest, AlertLogTagLogTest) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();

  auto category = "test_category";
  auto test_msg = "Test Error Message: ";
  auto test_id = 10;
  auto expected_log = base::StringPrintf(
      "%s%s%d", AlertLogTag(category).c_str(), test_msg, test_id);

  EXPECT_CALL(mock_log,
              Log(::logging::LOGGING_ERROR, _, _, _, HasSubstr(expected_log)));

  LOG(ERROR) << AlertLogTag(category) << test_msg << test_id;
}

TEST(UtilsTest, MountStatefulPartitionTest) {
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();

  std::vector<std::string> expected_args = {
      "/usr/bin/stateful_partition_for_recovery", "--mount"};

  EXPECT_CALL(*mock_process_manager_, RunCommand(expected_args, _))
      .WillOnce(::testing::Return(0));
  EXPECT_TRUE(MountStatefulPartition(mock_process_manager_));

  // Verify error results.
  EXPECT_CALL(*mock_process_manager_, RunCommand)
      .WillOnce(::testing::Return(1));
  EXPECT_FALSE(MountStatefulPartition(mock_process_manager_));
  EXPECT_FALSE(MountStatefulPartition(nullptr));
}

TEST(UtilsTest, UnmountPathTest) {
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  std::vector<std::string> expected_args = {"/bin/busybox", "umount",
                                            temp_dir.GetPath().value()};

  EXPECT_CALL(*mock_process_manager_, RunCommand(expected_args, _))
      .WillOnce(::testing::Return(0));

  EXPECT_TRUE(UnmountPath(mock_process_manager_, temp_dir.GetPath()));

  // Verify invalid process manager.
  EXPECT_FALSE(UnmountPath(nullptr, temp_dir.GetPath()));
}

TEST(UtilsTest, CompressLogsTest) {
  auto mock_process_manager = std::make_shared<MockProcessManager>();
  const auto archive_path = "/path/to/store/archive";
  std::vector<std::string> expected_cmd = {
      "/usr/bin/tar",         "-czhf",
      archive_path,           "/var/log/update_engine.log",
      "/var/log/upstart.log", "/var/log/messages"};
  EXPECT_CALL(*mock_process_manager, RunCommand(expected_cmd, _));

  CompressLogs(mock_process_manager, base::FilePath{archive_path});
}

TEST(UtilsTest, KernelSizeTest) {
  auto mock_process_manager = std::make_shared<MockProcessManager>();
  const auto device_path = "/dev/device0p1";
  std::vector<std::string> expected_cmd = {"/usr/bin/futility", "show", "-P",
                                           device_path};
  const std::string futility_output =
      std::string{"kernel_partition::/dev/nvme0n1p9\n"} +
      std::string{"kernel::keyblock::size::1\n"} +
      std::string{"kernel::preamble::size::10\n"} +
      std::string{"kernel::preamble::body::load_address::0x100000\n"} +
      std::string{"kernel::body::size::100\n"};

  EXPECT_CALL(*mock_process_manager,
              RunCommandWithOutput(expected_cmd, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      testing::Return(true)));

  EXPECT_THAT(KernelSize(mock_process_manager, base::FilePath{device_path}),
              Optional(111));
}

TEST(UtilsTest, KernelSizeFailuresTest) {
  auto mock_process_manager = std::make_shared<MockProcessManager>();
  const auto device_path = "/dev/device0p1";
  std::vector<std::string> expected_cmd = {"/usr/bin/futility", "show", "-P",
                                           device_path};
  // Test out empty string.
  std::string futility_output = "";
  EXPECT_CALL(*mock_process_manager,
              RunCommandWithOutput(expected_cmd, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      testing::Return(true)));
  EXPECT_EQ(KernelSize(mock_process_manager, base::FilePath{device_path}),
            std::nullopt);

  // Missing kernel body size.
  futility_output = std::string{"kernel::keyblock::size::2232\n"} +
                    std::string{"kernel::preamble::size::63304\n"};
  mock_process_manager = std::make_unique<MockProcessManager>();
  EXPECT_CALL(*mock_process_manager,
              RunCommandWithOutput(expected_cmd, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      testing::Return(true)));
  EXPECT_EQ(KernelSize(mock_process_manager, base::FilePath{device_path}),
            std::nullopt);

  // 0 kernel body size.
  futility_output = std::string{"kernel::keyblock::size::2232\n"} +
                    std::string{"kernel::preamble::size::63304\n"} +
                    std::string{"kernel::body::size::0\n"};
  mock_process_manager = std::make_unique<MockProcessManager>();
  EXPECT_CALL(*mock_process_manager,
              RunCommandWithOutput(expected_cmd, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      testing::Return(true)));
  EXPECT_EQ(KernelSize(mock_process_manager, base::FilePath{device_path}),
            std::nullopt);

  // Non number value for keyblock size.
  futility_output = std::string{"keyblock::size::bad_val\n"} +
                    std::string{"kernel::preamble::size::63304\n"} +
                    std::string{"kernel::preamble::body::size::50\n"};
  mock_process_manager = std::make_unique<MockProcessManager>();
  EXPECT_CALL(*mock_process_manager,
              RunCommandWithOutput(expected_cmd, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), SetArgPointee<2>(futility_output),
                      testing::Return(true)));
  EXPECT_EQ(KernelSize(mock_process_manager, base::FilePath{device_path}),
            std::nullopt);
}

TEST(UtilsTest, GetRemovableDevices) {
  auto device_list_entry =
      std::make_unique<NiceMock<brillo::MockUdevListEntry>>();
  auto device = std::make_unique<StrictMock<brillo::MockUdevDevice>>();

  auto mock_udev_enumerate =
      std::make_unique<StrictMock<brillo::MockUdevEnumerate>>();
  auto mock_udev = std::make_unique<StrictMock<brillo::MockUdev>>();
  constexpr auto& device_node = "/dev/sda1";

  EXPECT_CALL(*mock_udev_enumerate, AddMatchSubsystem(StrEq("block")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_udev_enumerate,
              AddMatchProperty(StrEq("ID_FS_USAGE"), StrEq("filesystem")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_udev_enumerate, ScanDevices()).WillOnce(Return(true));

  // Setup device to be removable.
  EXPECT_CALL(*device, GetSysAttributeValue(_)).WillOnce(Return("1"));
  EXPECT_CALL(*device, GetDeviceNode()).WillOnce(Return(device_node));
  EXPECT_CALL(*mock_udev_enumerate, GetListEntry())
      .WillOnce(Return(std::move(device_list_entry)));
  EXPECT_CALL(*mock_udev, CreateDeviceFromSysPath(_))
      .WillOnce(Return(std::move(device)));
  EXPECT_CALL(*mock_udev, CreateEnumerate())
      .WillOnce(Return(std::move(mock_udev_enumerate)));

  std::vector<base::FilePath> removable_devices;
  EXPECT_TRUE(GetRemovableDevices(removable_devices, std::move(mock_udev)));
  // Expect to get back the one device path we have setup.
  EXPECT_THAT(removable_devices, ElementsAre(base::FilePath{device_node}));
}

TEST(UtilsTest, GetLogStoreKeyTest) {
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();

  const std::vector<std::string>& kExpectedArgs = {"/usr/bin/vpd", "-g",
                                                   "minios_log_store_key"};

  EXPECT_CALL(*mock_process_manager_,
              RunCommandWithOutput(kExpectedArgs, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(0),
                      SetArgPointee<2>(
                          brillo::SecureBlobToSecureHex(kValidKey).to_string()),
                      ::testing::Return(true)));
  const auto log_store_key = GetLogStoreKey(mock_process_manager_);

  ASSERT_TRUE(log_store_key.has_value());
  EXPECT_EQ(log_store_key.value(), kValidKey);
}

TEST(UtilsTest, GetLogStoreKeyFailureTest) {
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();
  const std::string& kKey = "short_key";

  const std::vector<std::string> kExpectedArgs = {"/usr/bin/vpd", "-g",
                                                  "minios_log_store_key"};

  EXPECT_CALL(*mock_process_manager_,
              RunCommandWithOutput(kExpectedArgs, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(-1), SetArgPointee<2>(kKey),
                      ::testing::Return(true)));
  const auto log_store_key = GetLogStoreKey(mock_process_manager_);

  EXPECT_FALSE(log_store_key.has_value());
}

TEST(UtilsTest, LogStoreKeyValidTest) {
  const auto& kShortKey = brillo::SecureBlob{"short"};
  const auto& kLongKey =
      brillo::SecureBlob{"thisisa32bytestring1234567890abc_____"};
  const auto& kEmptyKey = brillo::SecureBlob{""};

  EXPECT_TRUE(IsLogStoreKeyValid(kValidKey));
  EXPECT_FALSE(IsLogStoreKeyValid(kShortKey));
  EXPECT_FALSE(IsLogStoreKeyValid(kLongKey));
  EXPECT_FALSE(IsLogStoreKeyValid(kEmptyKey));
}

TEST(UtilsTest, LogStoreKeyTrimTest) {
  std::string simple_key =
      "thisisa64bytestring1234567890abcthisisa64bytestring1234567890abc";
  TrimLogStoreKey(simple_key);
  EXPECT_EQ(simple_key,
            "thisisa64bytestring1234567890abcthisisa64bytestring1234567890abc");

  std::string short_key = "short_key";
  TrimLogStoreKey(short_key);
  EXPECT_EQ(short_key, "short_key");

  std::string simple_key_with_trailing_space = simple_key + "  ";
  TrimLogStoreKey(simple_key_with_trailing_space);
  EXPECT_EQ(simple_key_with_trailing_space, simple_key);

  std::string simple_key_with_whitespace = simple_key + "\n ";
  TrimLogStoreKey(simple_key_with_whitespace);
  EXPECT_EQ(simple_key_with_whitespace, simple_key);

  std::string key_with_whitespace =
      "thisisa64bytestring1234567890abcthisisa64bytestring1234567890\n  ";
  TrimLogStoreKey(key_with_whitespace);
  EXPECT_EQ(
      key_with_whitespace,
      "thisisa64bytestring1234567890abcthisisa64bytestring1234567890\n  ");

  std::string key_with_trailing_whitespace =
      "thisisa64bytestring1234567890abcthisisa64bytestring1234567890\n  \n\t ";
  TrimLogStoreKey(key_with_trailing_whitespace);
  EXPECT_EQ(
      key_with_trailing_whitespace,
      "thisisa64bytestring1234567890abcthisisa64bytestring1234567890\n  ");
}

TEST(UtilsTest, SaveLogKeyTest) {
  auto mock_process_manager_ =
      std::make_shared<StrictMock<MockProcessManager>>();

  const std::vector<std::string> expected_args = {
      "/usr/bin/vpd", "-s",
      "minios_log_store_key=" +
          brillo::SecureBlobToSecureHex(kValidKey).to_string()};

  EXPECT_CALL(*mock_process_manager_, RunCommand(expected_args, _))
      .WillOnce(::testing::Return(0));
  EXPECT_TRUE(SaveLogStoreKey(mock_process_manager_, kValidKey));
}

TEST(UtilsTest, EncryptDecryptTest) {
  const auto& encrypted_contents = EncryptLogArchiveData(kTestData, kValidKey);
  EXPECT_TRUE(encrypted_contents.has_value());
  const auto& decrypted_contents =
      DecryptLogArchiveData(encrypted_contents.value(), kValidKey);

  EXPECT_TRUE(decrypted_contents.has_value());
  EXPECT_EQ(kTestData, decrypted_contents);
}

TEST(UtilsTest, ReadFileToSecureBlobTest) {
  base::ScopedTempDir tmp_dir_;
  ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
  const auto file_path = tmp_dir_.GetPath().Append("file");

  ASSERT_TRUE(base::WriteFile(file_path, kTestData));

  const auto& file_contents = ReadFileToSecureBlob(file_path);
  EXPECT_TRUE(file_contents.has_value());
  EXPECT_EQ(file_contents.value(), kTestData);
}

TEST(UtilsTest, WriteSecureBlobToFileTest) {
  base::ScopedTempDir tmp_dir_;
  ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
  const auto file_path = tmp_dir_.GetPath().Append("file");
  EXPECT_TRUE(WriteSecureBlobToFile(file_path, kTestData));

  const auto& file_contents = ReadFileToSecureBlob(file_path);
  EXPECT_TRUE(file_contents.has_value());
  EXPECT_EQ(file_contents.value(), kTestData);
}

}  // namespace minios
