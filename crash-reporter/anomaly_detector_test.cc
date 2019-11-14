// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

namespace {

using std::string_literals::operator""s;

using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SizeIs;

using anomaly::KernelParser;
using anomaly::SELinuxParser;
using anomaly::ServiceParser;
using anomaly::TerminaParser;

std::vector<std::string> GetTestLogMessages(base::FilePath input_file) {
  std::string contents;
  // Though ASSERT would be better here, we need to use EXPECT since functions
  // calling ASSERT must return void.
  EXPECT_TRUE(base::ReadFileToString(input_file, &contents));
  auto log_msgs = base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                                    base::SPLIT_WANT_ALL);
  EXPECT_GT(log_msgs.size(), 0);
  // Handle likely newline at end of file.
  if (log_msgs.back().empty())
    log_msgs.pop_back();
  return log_msgs;
}

void ReplaceMsgContent(std::vector<std::string>* log_msgs,
                       const std::string& find_this,
                       const std::string& replace_with) {
  for (auto& msg : *log_msgs)
    base::ReplaceSubstringsAfterOffset(&msg, 0, find_this, replace_with);
}

std::vector<anomaly::CrashReport> ParseLogMessages(
    anomaly::Parser* parser, const std::vector<std::string>& log_msgs) {
  std::vector<anomaly::CrashReport> crash_reports;
  for (auto& msg : log_msgs) {
    auto crash_report = parser->ParseLogEntry(msg);
    if (crash_report)
      crash_reports.push_back(std::move(*crash_report));
  }
  return crash_reports;
}

using base::nullopt;

struct ParserRun {
  base::Optional<std::string> find_this = nullopt;
  base::Optional<std::string> replace_with = nullopt;
  base::Optional<std::string> expected_text = nullopt;
  base::Optional<std::string> expected_flag = nullopt;
  size_t expected_size = 1;
};

const ParserRun simple_run;

template <class T>
void ParserTest(const std::string& input_file_name,
                std::initializer_list<ParserRun> parser_runs) {
  T parser;

  auto log_msgs =
      GetTestLogMessages(test_util::GetTestDataPath(input_file_name));
  for (auto& run : parser_runs) {
    if (run.find_this && run.replace_with)
      ReplaceMsgContent(&log_msgs, *run.find_this, *run.replace_with);
    auto crash_reports = ParseLogMessages(&parser, log_msgs);

    ASSERT_THAT(crash_reports, SizeIs(run.expected_size));
    if (run.expected_text)
      EXPECT_THAT(crash_reports[0].text, HasSubstr(*run.expected_text));
    if (run.expected_flag)
      EXPECT_EQ(crash_reports[0].flag, *run.expected_flag);
  }
}

}  // namespace

TEST(AnomalyDetectorTest, KernelWarning) {
  ParserRun second{.find_this = "ttm_bo_vm.c"s,
                   .replace_with = "file_one.c"s,
                   .expected_text = "0x19e/0x1ab [ttm]()\nModules linked in"s};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, second});
}

TEST(AnomalyDetectorTest, KernelWarningNoDuplicate) {
  ParserRun identical_warning{.expected_size = 0};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, identical_warning});
}

TEST(AnomalyDetectorTest, KernelWarningHeader) {
  ParserRun warning_message{.expected_text = "Test Warning message asdfghjkl"s};
  ParserTest<KernelParser>("TEST_WARNING_HEADER", {warning_message});
}

TEST(AnomalyDetectorTest, KernelWarningOld) {
  ParserTest<KernelParser>("TEST_WARNING_OLD", {simple_run});
}

TEST(AnomalyDetectorTest, KernelWarningOldARM64) {
  ParserRun unknown_function{.expected_text = "-unknown-function\n"s};
  ParserTest<KernelParser>("TEST_WARNING_OLD_ARM64", {unknown_function});
}

TEST(AnomalyDetectorTest, KernelWarningWifi) {
  ParserRun wifi_warning = {.find_this = "gpu/drm/ttm"s,
                            .replace_with = "net/wireless"s,
                            .expected_flag = "--kernel_wifi_warning"s};
  ParserTest<KernelParser>("TEST_WARNING", {wifi_warning});
}

TEST(AnomalyDetectorTest, KernelWarningSuspend) {
  ParserRun suspend_warning = {.find_this = "gpu/drm/ttm"s,
                               .replace_with = "idle"s,
                               .expected_flag = "--kernel_suspend_warning"s};
  ParserTest<KernelParser>("TEST_WARNING", {suspend_warning});
}

TEST(AnomalyDetectorTest, CrashReporterCrash) {
  ParserRun crash_reporter_crash = {.expected_flag =
                                        "--crash_reporter_crashed"s};
  ParserTest<KernelParser>("TEST_CR_CRASH", {crash_reporter_crash});
}

TEST(AnomalyDetectorTest, CrashReporterCrashRateLimit) {
  ParserRun crash_reporter_crash = {.expected_flag =
                                        "--crash_reporter_crashed"s};
  ParserRun empty = {.expected_size = 0};
  ParserTest<KernelParser>("TEST_CR_CRASH",
                           {crash_reporter_crash, empty, empty});
}

TEST(AnomalyDetectorTest, ServiceFailure) {
  ParserRun one{.expected_text = "-exit2-"s};
  ParserRun two{.find_this = "crash-crash"s, .replace_with = "fresh-fresh"s};
  ParserTest<ServiceParser>("TEST_SERVICE_FAILURE", {one, two});
}

TEST(AnomalyDetectorTest, ServiceFailureArc) {
  ParserRun service_failure = {
      .find_this = "crash-crash"s,
      .replace_with = "arc-crash"s,
      .expected_text = "-exit2-arc-"s,
      .expected_flag = "--arc_service_failure=arc-crash"s};
  ParserTest<ServiceParser>("TEST_SERVICE_FAILURE", {service_failure});
}

TEST(AnomalyDetectorTest, SELinuxViolation) {
  ParserRun selinux_violation = {
      .expected_text =
          "-selinux-u:r:init:s0-u:r:kernel:s0-module_request-init-"s,
      .expected_flag = "--selinux_violation"s};
  ParserTest<SELinuxParser>("TEST_SELINUX", {selinux_violation});
}

MATCHER_P2(SignalEq, interface, member, "") {
  return (arg->GetInterface() == interface && arg->GetMember() == member);
}

TEST(AnomalyDetectorTest, BTRFSExtentCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(options);

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(1);

  TerminaParser parser(bus);

  parser.ParseLogEntry(
      "VM(3)",
      "BTRFS warning (device vdb): csum failed root 5 ino 257 off 409600 csum "
      "0x76ad9387 expected csum 0xd8d34542 mirror 1");
}

TEST(AnomalyDetectorTest, BTRFSTreeCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(options);

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(1);

  TerminaParser parser(bus);

  parser.ParseLogEntry("VM(3)",
                       "BTRFS warning (device vdb): vdb checksum verify failed "
                       "on 122798080 wanted 4E5B4C99 found 5F261FEB level 0");
}
