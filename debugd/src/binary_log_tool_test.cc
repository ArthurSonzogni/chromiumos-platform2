// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-proxies.h>
#include <fbpreprocessor-client-test/fbpreprocessor/dbus-proxy-mocks.h>

#include <gtest/gtest.h>

#include "debugd/src/binary_log_tool.h"
#include "debugd/src/sandboxed_process.h"

using testing::_;
using testing::Invoke;
using testing::WithArg;

namespace {

constexpr std::string_view kDefaultUserhash("0abcdef1230abcdef1230abcdef123");
constexpr std::string_view kDefaultTestData("test data");
constexpr std::string_view kDefaultOutputFile("output.tar.zst");

constexpr int kIncorrectBinaryLogType = -1;

void CreateFiles(const std::set<base::FilePath>& files,
                 const std::string_view& data) {
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

}  // namespace

namespace debugd {

class BinaryLogToolTest : public testing::Test {
 protected:
  org::chromium::FbPreprocessorProxyMock* GetFbPreprocessorProxy() {
    return static_cast<org::chromium::FbPreprocessorProxyMock*>(
        binary_log_tool_->GetFbPreprocessorProxyForTesting());
  }

  base::FilePath InputDirectory() const {
    return daemon_store_base_dir_.GetPath()
        .Append(kDefaultUserhash)
        .Append(fbpreprocessor::kProcessedDirectory);
  }

  base::FilePath OutputFile() const {
    return output_dir_.GetPath().Append(kDefaultOutputFile);
  }

  void SimulateDaemonDBusResponses(
      const fbpreprocessor::DebugDumps& input_debug_dumps,
      const std::string_view& userhash) {
    ON_CALL(*GetFbPreprocessorProxy(), GetDebugDumps(_, _, _))
        .WillByDefault(WithArg<0>(Invoke(
            [&input_debug_dumps](fbpreprocessor::DebugDumps* out_DebugDumps) {
              *out_DebugDumps = input_debug_dumps;
              return true;
            })));
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

    binary_log_tool_ = std::unique_ptr<BinaryLogTool>(new BinaryLogTool(
        std::make_unique<org::chromium::FbPreprocessorProxyMock>()));
  }

  std::unique_ptr<BinaryLogTool> binary_log_tool_;

  base::ScopedTempDir daemon_store_base_dir_;
  base::ScopedTempDir output_dir_;
};

// This test requests an invalid FeedbackBinaryLogType to GetBinaryLogs().
// Verify that nothing is written to the file descriptor.
TEST_F(BinaryLogToolTest, IncorrectBinaryLogTypeDoesNotWriteToFD) {
  base::FilePath file_path(InputDirectory().Append("test_file.txt"));
  std::set<base::FilePath> input_files = {file_path};
  CreateFiles(input_files, kDefaultTestData);

  fbpreprocessor::DebugDumps input_dumps = CreateProtobuf(input_files);

  SimulateDaemonDBusResponses(input_dumps, kDefaultUserhash);

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
  fbpreprocessor::DebugDumps input_dumps;

  SimulateDaemonDBusResponses(input_dumps, kDefaultUserhash);

  WriteBinaryLogsToOutputFile(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP);

  std::optional<std::vector<uint8_t>> bytes = ReadFileToBytes(OutputFile());
  ASSERT_TRUE(bytes.has_value());
  EXPECT_TRUE(bytes.value().empty());
}

}  // namespace debugd
