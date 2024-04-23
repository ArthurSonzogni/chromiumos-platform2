// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/vm_collector.h"

#include <limits>
#include <memory>

#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/strcat.h>
#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>
#include <vm_protos/proto_bindings/vm_crash.grpc.pb.h>

#include "base/files/file_util.h"
#include "crash-reporter/constants.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using ::testing::AllOf;
using ::testing::HasSubstr;

class VmCollectorTest : public ::testing::Test {
 public:
  VmCollectorTest()
      : collector_(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(temp_dir_.GetPath());
    collector_.set_crash_directory_for_test(temp_dir_.GetPath());
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

 protected:
  base::ScopedTempDir temp_dir_;
  VmCollector collector_;
};

TEST_F(VmCollectorTest, SuccessfulCollect) {
  vm_tools::cicerone::CrashReport crash_report;
  constexpr char kProcessTree[] =
      "USER  PID %CPU %MEM    VSZ   RSS TTY  STAT START TIME COMMAND\n"
      "root    1  0.0  0.0  23292 14400 ?    Ss   11:46 0:17 /sbin/init splash";
  crash_report.set_process_tree(kProcessTree);
  std::string minidump;
  // minidump can be binary.
  for (char c = std::numeric_limits<char>::min();
       c < std::numeric_limits<char>::max(); ++c) {
    minidump.push_back(c);
  }
  minidump.push_back(std::numeric_limits<char>::max());
  crash_report.set_minidump(minidump);

  crash_report.mutable_metadata()->emplace(
      base::StrCat({constants::kUploadVarPrefix, "pid"}), "88");
  crash_report.mutable_metadata()->emplace(
      base::StrCat({constants::kUploadVarPrefix, "collector"}), "user");
  crash_report.mutable_metadata()->emplace(
      base::StrCat({constants::kUploadVarPrefix, "client_computed_serevity"}),
      "ERROR");
  crash_report.mutable_metadata()->emplace("done", "1");

  base::FilePath proto_input_path = temp_dir_.GetPath().Append("proto_input");
  std::string text_format;
  ASSERT_TRUE(
      google::protobuf::TextFormat::PrintToString(crash_report, &text_format));
  ASSERT_TRUE(base::WriteFile(proto_input_path, text_format));

  base::File proto_input(proto_input_path,
                         base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(proto_input.IsValid());

  EXPECT_TRUE(collector_.CollectFromFile(
      88,
      google::protobuf::io::FileInputStream(proto_input.GetPlatformFile())));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      temp_dir_.GetPath(), "vm_crash.*.88.meta", &meta_path));
  base::FilePath payload_path =
      meta_path.RemoveFinalExtension().AddExtension("dmp");
  base::FilePath process_log_path =
      meta_path.RemoveFinalExtension().AddExtension("proclog");
  std::string meta_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(
      meta_contents,
      AllOf(HasSubstr("upload_var_pid=88\n"),
            HasSubstr("upload_var_collector=user\n"),
            HasSubstr("upload_var_client_computed_serevity=ERROR\n"),
            HasSubstr("payload=" + payload_path.BaseName().value() + "\n"),
            HasSubstr("upload_file_process_tree=" +
                      process_log_path.BaseName().value() + "\n"),
            HasSubstr("done=1\n")));

  std::string process_log_contents;
  EXPECT_TRUE(base::ReadFileToString(process_log_path, &process_log_contents));
  EXPECT_EQ(process_log_contents, kProcessTree);

  std::string payload_contents;
  EXPECT_TRUE(base::ReadFileToString(payload_path, &payload_contents));
  EXPECT_EQ(payload_contents, minidump);
}

TEST_F(VmCollectorTest, BadProto) {
  base::FilePath proto_input_path = temp_dir_.GetPath().Append("proto_input");
  std::string text_format = "{{{{{";
  ASSERT_TRUE(base::WriteFile(proto_input_path, text_format));

  base::File proto_input(proto_input_path,
                         base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(proto_input.IsValid());

  EXPECT_FALSE(collector_.CollectFromFile(
      88,
      google::protobuf::io::FileInputStream(proto_input.GetPlatformFile())));

  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      temp_dir_.GetPath(), "vm_crash.*.88.meta", nullptr));
}

TEST_F(VmCollectorTest, ComputeSeverity) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("any executable");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kError);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}
