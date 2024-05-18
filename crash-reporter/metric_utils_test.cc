// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/metric_utils.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process_mock.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector_names.h"
#include "crash-reporter/crash_sending_mode.h"
#include "crash-reporter/util.h"

// Metrics are not recorded inside a VM since there is nowhere to record them.
// TODO(b/343493432): Record metrics inside VMs
#if USE_KVM_GUEST
#define DISABLED_IN_VM(name) DISABLED_##name
#define ONLY_IN_VM(name) name
#else
#define DISABLED_IN_VM(name) name
#define ONLY_IN_VM(name) DISABLED_##name
#endif

namespace {
using ::testing::_;
using ::testing::A;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Not;

// Overrides the normal factory to inject a ProcessMock instead of a real
// Process.
class MockBrilloProcessFactory : public util::BrilloProcessFactory {
 public:
  std::unique_ptr<brillo::Process> CreateProcess() override {
    return std::move(mock_process_);
  }

  // This will be set to nullptr when a Record... function runs.
  std::unique_ptr<brillo::ProcessMock> mock_process_ =
      std::make_unique<brillo::ProcessMock>();
};

class MetricUtilsTest : public ::testing::Test {
 public:
  void SetUp() override {
    OverrideBrilloProcessFactoryForTesting(&factory_);
    brillo::ClearLog();
  }

  void TearDown() override { OverrideBrilloProcessFactoryForTesting(nullptr); }

  // Intended to be used as the Invoke'd function from the mock
  // Process::RedirectOutput. Will save the path into output_file_path_.
  void RedirectOutputFaker(const base::FilePath& path) {
    output_file_path_ = path;
  }

  // Intended to be used as the Invoke'd function from the mock
  // Process::Run. Will create a file at the output_file_path_
  // with output_file_contents_ as the contents and then return run_result_.
  int RunFaker() {
    if (delete_output_file_) {
      CHECK(brillo::DeleteFile(output_file_path_));
      return run_result_;
    }
    base::File output(output_file_path_,
                      base::File::FLAG_OPEN | base::File::FLAG_WRITE);
    CHECK(output.IsValid())
        << base::File::ErrorToString(output.error_details());
    // File should exist but be empty before Run is called.
    base::File::Info info;
    CHECK(output.GetInfo(&info));
    CHECK(!info.is_directory);
    CHECK(!info.is_symbolic_link);
    CHECK_EQ(info.size, 0);
    CHECK_EQ(output.WriteAtCurrentPos(output_file_contents_.c_str(),
                                      output_file_contents_.length()),
             output_file_contents_.length());

    return run_result_;
  }

 protected:
  // Contents of output file after the mock_process_ is run.
  std::string output_file_contents_;

  // Path that was used as temporary storage for metric_client's output.
  base::FilePath output_file_path_;

  // Return code to return from Run().
  int run_result_ = 0;

  // If true, RunFaker will just delete the output file instead.
  bool delete_output_file_ = false;

  MockBrilloProcessFactory factory_;
  // Each MockBrilloProcessFactory can only be used once since it moves the
  // mock process out, so make another one for the
  // call to CrashReporterStatusRecorder::~CrashReporterStatusRecorder.
  MockBrilloProcessFactory factory_for_destructor_;
};

TEST_F(MetricUtilsTest, DISABLED_IN_VM(RecordCrashReporterStart_Success)) {
  constexpr char kInfoMessage2[] = "info message 2";
  {
    InSequence seq;  // Order of args is important!
    EXPECT_CALL(*factory_.mock_process_, AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--structured")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporting")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporterStart")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--Collector")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("8")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--CrashSendingMode")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = 0;
    constexpr char kInfoMessage[] = "info message";
    output_file_contents_ = kInfoMessage;
    EXPECT_CALL(*factory_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));

    // CrashReporterCollector::kGenericFailure = 8
    CrashReporterStatusRecorder recorder = RecordCrashReporterStart(
        CrashReporterCollector::kGenericFailure, CrashSendingMode::kNormal);

    EXPECT_THAT(brillo::GetLog(), Not(HasSubstr("Failed to invoke")));
    // If metrics_client prints a info message but succeeds, we don't want to
    // log that. We only log on failures.
    EXPECT_THAT(brillo::GetLog(), Not(HasSubstr(kInfoMessage)));

    // Ensure the temp output file was cleaned up.
    EXPECT_FALSE(base::PathExists(output_file_path_));

    // Set up new expectations for
    // CrashReporterStatusRecorder::~CrashReporterStatusRecorder()
    recorder.set_status(CrashCollectionStatus::kOutOfCapacity);
    EXPECT_EQ(recorder.status(), CrashCollectionStatus::kOutOfCapacity);

    OverrideBrilloProcessFactoryForTesting(&factory_for_destructor_);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--structured"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporting"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporterStatus"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Status"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("400")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Collector"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("8")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("--CrashSendingMode"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = 0;
    output_file_contents_ = kInfoMessage2;
    EXPECT_CALL(*factory_for_destructor_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));
    brillo::ClearLog();
  }
  EXPECT_THAT(brillo::GetLog(), Not(HasSubstr("Failed to invoke")));
  // If metrics_client prints a info message but succeeds, we don't want to log
  // that. We only log on failures.
  EXPECT_THAT(brillo::GetLog(), Not(HasSubstr(kInfoMessage2)));

  // Ensure the temp output file was cleaned up.
  EXPECT_FALSE(base::PathExists(output_file_path_));
}

TEST_F(MetricUtilsTest,
       DISABLED_IN_VM(
           CrashReporterStatusRecorder_UsesUnknownStatusIfSetStatusNotCalled)) {
  constexpr char kInfoMessage[] = "info message";
  {
    InSequence seq;  // Order of args is important!
    EXPECT_CALL(*factory_.mock_process_, AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--structured")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporting")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporterStart")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--Collector")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("8")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--CrashSendingMode")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = 0;
    output_file_contents_ = kInfoMessage;
    EXPECT_CALL(*factory_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));

    // CrashReporterCollector::kGenericFailure = 8
    CrashReporterStatusRecorder recorder = RecordCrashReporterStart(
        CrashReporterCollector::kGenericFailure, CrashSendingMode::kNormal);

    EXPECT_THAT(brillo::GetLog(), Not(HasSubstr("Failed to invoke")));
    // If metrics_client prints a info message but succeeds, we don't want to
    // log that. We only log on failures.
    EXPECT_THAT(brillo::GetLog(), Not(HasSubstr(kInfoMessage)));

    // Ensure the temp output file was cleaned up.
    EXPECT_FALSE(base::PathExists(output_file_path_));

    // Set up new expectations for
    // CrashReporterStatusRecorder::~CrashReporterStatusRecorder()
    EXPECT_EQ(recorder.status(), CrashCollectionStatus::kUnknownStatus);

    OverrideBrilloProcessFactoryForTesting(&factory_for_destructor_);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--structured"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporting"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporterStatus"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Status"))
        .Times(1);
    // CrashCollectionStatus::kUnknownStatus = 200
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("200")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Collector"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("8")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("--CrashSendingMode"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    EXPECT_CALL(*factory_for_destructor_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));
    brillo::ClearLog();
  }
  EXPECT_THAT(brillo::GetLog(), Not(HasSubstr("Failed to invoke")));
  // If metrics_client prints a info message but succeeds, we don't want to log
  // that. We only log on failures.
  EXPECT_THAT(brillo::GetLog(), Not(HasSubstr(kInfoMessage)));

  // Ensure the temp output file was cleaned up.
  EXPECT_FALSE(base::PathExists(output_file_path_));
}

TEST_F(MetricUtilsTest, DISABLED_IN_VM(RecordCrashReporterStart_RunFailure)) {
  {
    InSequence seq;  // Order of args is important!
    EXPECT_CALL(*factory_.mock_process_, AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--structured")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporting")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporterStart")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--Collector")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("2")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--CrashSendingMode")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("1")).Times(1);
    EXPECT_CALL(*factory_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = 5;
    constexpr char kErrorMessage[] = "Unknown arg --Collector";
    output_file_contents_ = kErrorMessage;
    EXPECT_CALL(*factory_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));

    // CrashReporterCollector::kChrome = 2
    CrashReporterStatusRecorder recorder = RecordCrashReporterStart(
        CrashReporterCollector::kChrome, CrashSendingMode::kCrashLoop);

    constexpr char kExpectedMessage[] =
        "Failed to invoke /usr/bin/metrics_client --structured "
        "CrashReporting CrashReporterStart --Collector 2 --CrashSendingMode 1: "
        "exit code 5 with output: Unknown arg --Collector";
    EXPECT_THAT(brillo::GetLog(), HasSubstr(kExpectedMessage));

    // Ensure the temp output file was cleaned up.
    EXPECT_FALSE(base::PathExists(output_file_path_));

    // Set up new expectations for
    // CrashReporterStatusRecorder::~CrashReporterStatusRecorder()
    recorder.set_status(CrashCollectionStatus::kSuccess);
    EXPECT_EQ(recorder.status(), CrashCollectionStatus::kSuccess);

    OverrideBrilloProcessFactoryForTesting(&factory_for_destructor_);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--structured"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporting"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporterStatus"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Status"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Collector"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("2")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("--CrashSendingMode"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("1")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = 7;
    constexpr char kErrorMessage2[] = "Unknown arg --Status";
    output_file_contents_ = kErrorMessage2;
    EXPECT_CALL(*factory_for_destructor_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));
    brillo::ClearLog();
  }
  constexpr char kExpectedMessage[] =
      "Failed to invoke /usr/bin/metrics_client --structured "
      "CrashReporting CrashReporterStatus --Status 0 --Collector 2 "
      "--CrashSendingMode 1: exit code 7 with output: Unknown arg --Status";
  EXPECT_THAT(brillo::GetLog(), HasSubstr(kExpectedMessage));

  // Ensure the temp output file was cleaned up.
  EXPECT_FALSE(base::PathExists(output_file_path_));
}

TEST_F(MetricUtilsTest,
       DISABLED_IN_VM(RecordCrashReporterStart_RunFailureNoOutputFile)) {
  {
    InSequence seq;  // Order of args is important!
    EXPECT_CALL(*factory_.mock_process_, AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--structured")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporting")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("CrashReporterStart")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--Collector")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("2")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("--CrashSendingMode")).Times(1);
    EXPECT_CALL(*factory_.mock_process_, AddArg("1")).Times(1);
    EXPECT_CALL(*factory_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    run_result_ = -1;
    delete_output_file_ = true;
    EXPECT_CALL(*factory_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));

    // CrashReporterCollector::kChrome = 2
    CrashReporterStatusRecorder recorder = RecordCrashReporterStart(
        CrashReporterCollector::kChrome, CrashSendingMode::kCrashLoop);

    constexpr char kExpectedMessage[] =
        "Failed to invoke /usr/bin/metrics_client --structured "
        "CrashReporting CrashReporterStart --Collector 2 --CrashSendingMode 1: "
        "exit code -1 with output: <could not read temp output file>";
    EXPECT_THAT(brillo::GetLog(), HasSubstr(kExpectedMessage));

    // Set up new expectations for
    // CrashReporterStatusRecorder::~CrashReporterStatusRecorder()
    recorder.set_status(CrashCollectionStatus::kSuccess);

    OverrideBrilloProcessFactoryForTesting(&factory_for_destructor_);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("/usr/bin/metrics_client"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--structured"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporting"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("CrashReporterStatus"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Status"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("0")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("--Collector"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("2")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                AddArg("--CrashSendingMode"))
        .Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_, AddArg("1")).Times(1);
    EXPECT_CALL(*factory_for_destructor_.mock_process_,
                RedirectOutput(A<const base::FilePath&>()))
        .WillOnce(Invoke(this, &MetricUtilsTest::RedirectOutputFaker));
    EXPECT_CALL(*factory_for_destructor_.mock_process_, Run())
        .WillOnce(Invoke(this, &MetricUtilsTest::RunFaker));
    brillo::ClearLog();
  }
  constexpr char kExpectedMessage[] =
      "Failed to invoke /usr/bin/metrics_client --structured "
      "CrashReporting CrashReporterStatus --Status 0 --Collector 2 "
      "--CrashSendingMode 1: exit code -1 with output: "
      "<could not read temp output file>";
  EXPECT_THAT(brillo::GetLog(), HasSubstr(kExpectedMessage));
}

TEST_F(MetricUtilsTest, ONLY_IN_VM(RecordCrashReporterStart_DoesNothingInVM)) {
  EXPECT_CALL(*factory_.mock_process_, AddArg(_)).Times(0);
  EXPECT_CALL(*factory_.mock_process_,
              RedirectOutput(A<const base::FilePath&>()))
      .Times(0);
  EXPECT_CALL(*factory_.mock_process_, Run()).Times(0);
  {
    CrashReporterStatusRecorder recorder = RecordCrashReporterStart(
        CrashReporterCollector::kArcJava, CrashSendingMode::kNormal);
  }
}
}  // namespace
