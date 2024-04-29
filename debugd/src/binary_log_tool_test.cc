// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <chromeos/dbus/debugd/dbus-constants.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client-test/fbpreprocessor/dbus-proxy-mocks.h>
#include <user_data_auth-client-test/user_data_auth/dbus-proxy-mocks.h>

#include <gtest/gtest.h>

#include "debugd/src/binary_log_tool.h"

using testing::_;

namespace {

constexpr std::string_view kDefaultUserhash("0abcdef1230abcdef1230abcdef123");
constexpr std::string_view kDefaultTestData("test data");
constexpr std::string_view kDefaultOutputFile("output.tar.zst");

constexpr int kIncorrectBinaryLogType = -1;

void CreateFiles(const std::set<base::FilePath>& files, std::string_view data) {
  for (auto file : files) {
    CHECK(base::WriteFile(file, data));
  }
}

fbpreprocessor::DebugDumps CreateProtobuf(
    const std::set<base::FilePath>& files) {
  fbpreprocessor::DebugDumps dump_files;

  for (auto file : files) {
    auto debug_dump = dump_files.add_dump();
    debug_dump->set_type(fbpreprocessor::DebugDump::WIFI);
    auto wifi_dump = debug_dump->mutable_wifi_dump();
    wifi_dump->set_dmpfile(file.value());
    wifi_dump->set_state(fbpreprocessor::WiFiDump::RAW);
    wifi_dump->set_vendor(fbpreprocessor::WiFiDump::IWLWIFI);
    wifi_dump->set_compression(fbpreprocessor::WiFiDump::GZIP);
  }

  return dump_files;
}

bool DecompressFile(const base::FilePath& file,
                    const base::FilePath& output_dir) {
  brillo::ProcessImpl p;

  p.AddArg("/bin/tar");
  p.AddArg("-xf");
  p.AddArg(file.value());
  p.AddArg("-C");
  p.AddArg(output_dir.value());

  return p.Run() == EXIT_SUCCESS;
}

}  // namespace

namespace debugd {

class BinaryLogToolTest : public testing::Test {
 protected:
  base::FilePath InputDirectory() const {
    return daemon_store_base_dir_.GetPath()
        .Append(kDefaultUserhash)
        .Append(fbpreprocessor::kProcessedDirectory);
  }

  base::FilePath OutputFile() const {
    return output_dir_.GetPath().Append(kDefaultOutputFile);
  }

  void SimulateDaemonDBusResponses(const std::set<base::FilePath>& files,
                                   std::string_view userhash) {
    fbpreprocessor::DebugDumps dump_files = CreateProtobuf(files);
    ON_CALL(*fbpreprocessor_proxy_, GetDebugDumps(_, _, _))
        .WillByDefault(testing::WithArg<0>(
            [dump_files](fbpreprocessor::DebugDumps* out_DebugDumps) {
              *out_DebugDumps = dump_files;
              return true;
            }));

    ON_CALL(*cryptohome_proxy_, GetSanitizedUsername(_, _, _, _))
        .WillByDefault(testing::WithArg<1>(
            [userhash](user_data_auth::GetSanitizedUsernameReply* reply) {
              reply->set_sanitized_username(userhash);
              return true;
            }));
  }

  void WriteBinaryLogsToOutputFile(const FeedbackBinaryLogType log_type) {
    base::ScopedFILE file(base::OpenFile(OutputFile(), "w"));
    ASSERT_NE(file, nullptr);

    std::map<FeedbackBinaryLogType, base::ScopedFD> outfds;
    outfds[log_type] = base::ScopedFD(fileno(file.get()));

    binary_log_tool_->DisableMinijailForTesting();
    binary_log_tool_->GetBinaryLogs("test_username", outfds);
  }

 private:
  void SetUpFakeDaemonStore() {
    CHECK(daemon_store_base_dir_.CreateUniqueTempDir());
    CHECK(base::CreateDirectory(InputDirectory()));
    CHECK(
        base::CreateDirectory(daemon_store_base_dir_.GetPath()
                                  .Append(kDefaultUserhash)
                                  .Append(fbpreprocessor::kScratchDirectory)));
  }

  void SetUp() override {
    CHECK(output_dir_.CreateUniqueTempDir());
    SetUpFakeDaemonStore();

    binary_log_tool_ = std::make_unique<BinaryLogTool>(
        base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options()));

    auto fbpreprocessor_proxy =
        std::make_unique<org::chromium::FbPreprocessorProxyMock>();
    fbpreprocessor_proxy_ = fbpreprocessor_proxy.get();
    binary_log_tool_->SetFbPreprocessorProxyForTesting(
        std::move(fbpreprocessor_proxy));

    auto cryptohome_proxy =
        std::make_unique<org::chromium::CryptohomeMiscInterfaceProxyMock>();
    cryptohome_proxy_ = cryptohome_proxy.get();
    binary_log_tool_->SetCryptohomeProxyForTesting(std::move(cryptohome_proxy));

    binary_log_tool_->SetDaemonStoreBaseDirForTesting(
        daemon_store_base_dir_.GetPath());
  }

  std::unique_ptr<BinaryLogTool> binary_log_tool_;
  // Owned by |binary_log_tool_|.
  org::chromium::FbPreprocessorProxyMock* fbpreprocessor_proxy_;
  // Owned by |binary_log_tool_|.
  org::chromium::CryptohomeMiscInterfaceProxyMock* cryptohome_proxy_;

  base::ScopedTempDir daemon_store_base_dir_;
  base::ScopedTempDir output_dir_;
};

// This test requests an invalid FeedbackBinaryLogType to GetBinaryLogs().
// Verify that nothing is written to the file descriptor.
TEST_F(BinaryLogToolTest, IncorrectBinaryLogTypeDoesNotWriteToFD) {
  base::FilePath file_path(InputDirectory().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  // Use incorrect FeedbackBinaryLogType
  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType(kIncorrectBinaryLogType));

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// GetDebugDumps() returns an empty list of dump files to GetBinaryLogs().
// Verify that nothing is written to the file descriptor.
TEST_F(BinaryLogToolTest, EmptyDumpsListDoesNotWriteToFD) {
  // Use an empty list of dump files
  std::set<base::FilePath> input_files;

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// GetSanitizedUsername() returns empty userhash to GetBinaryLogs(). No further
// processing can be done. Verify that nothing is written to file descriptor.
TEST_F(BinaryLogToolTest, EmptyUserhashDoesNotWriteToFD) {
  base::FilePath file_path(InputDirectory().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  // Use empty userhash
  SimulateDaemonDBusResponses(input_files, "");

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// If the primary userhash doesn't match the userhash in the input file's
// location, no further processing can be done. Verify that nothing is written
// to the output file descriptor.
TEST_F(BinaryLogToolTest, IncorrectUserhashDirDoesNotWriteToFD) {
  base::FilePath file_path(InputDirectory().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  // Use incorrect userhash
  SimulateDaemonDBusResponses(input_files, "test_userhash");

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// Process input files only from <daemon_store>/<userhash>/processed_dumps. If
// input files are from some other location, no further processing can be done.
// Verify that nothing is written to the file descriptor.
TEST_F(BinaryLogToolTest, IncorrectProcessedDirDoesNotWriteToFD) {
  // Use a different directory for input file instead of the
  // <daemon_store>/<userhash>/processed_dumps
  base::ScopedTempDir test_dir;
  ASSERT_TRUE(test_dir.CreateUniqueTempDir());

  base::FilePath file_path(test_dir.GetPath().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// If the daemon-store base directory is incorrect, no further processing can be
// done. Verify that nothing is written to the output file descriptor.
TEST_F(BinaryLogToolTest, IncorrectDaemonStoreDirDoesNotWriteToFD) {
  // Use a different daemon-store base directory
  base::ScopedTempDir base_dir;
  ASSERT_TRUE(base_dir.CreateUniqueTempDir());
  base::FilePath input_directory(
      base_dir.GetPath()
          .Append(kDefaultUserhash)
          .Append(fbpreprocessor::kProcessedDirectory));
  ASSERT_TRUE(base::CreateDirectory(input_directory));

  base::FilePath file_path(input_directory.Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// If the scratch directory is not present, no further processing can be done.
// Verify that nothing is written to the output file descriptor.
TEST_F(BinaryLogToolTest, NoScratchDirDoesNotWriteToFD) {
  // Delete the scratch directory for this test
  ASSERT_TRUE(brillo::DeleteFile(
      InputDirectory().DirName().Append(fbpreprocessor::kScratchDirectory)));

  base::FilePath file_path(InputDirectory().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

// This test verifies that the correct compressed binary logs are written to
// the output file descriptor in an ideal case.
TEST_F(BinaryLogToolTest, ValidInputWriteCompressedLogsToFD) {
  base::FilePath file_path_1(InputDirectory().Append("test_file_1.txt"));
  base::FilePath file_path_2(InputDirectory().Append("test_file_2.txt"));
  std::set<base::FilePath> input_files = {file_path_1, file_path_2};
  CreateFiles(input_files, kDefaultTestData);

  SimulateDaemonDBusResponses(input_files, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  // Extract the OutputFile() tarball in a new directory, verify that the two
  // files are in there and verify that the contents of those files match the
  // contents of the corresponding input files.
  base::ScopedTempDir output_dir;
  ASSERT_TRUE(output_dir.CreateUniqueTempDir());

  ASSERT_TRUE(DecompressFile(OutputFile(), output_dir.GetPath()));

  std::string out_data;
  ASSERT_TRUE(base::ReadFileToString(
      output_dir.GetPath().Append(file_path_1.BaseName()), &out_data));
  EXPECT_EQ(out_data, kDefaultTestData);

  out_data.clear();
  ASSERT_TRUE(base::ReadFileToString(
      output_dir.GetPath().Append(file_path_2.BaseName()), &out_data));
  EXPECT_EQ(out_data, kDefaultTestData);
}

}  // namespace debugd
