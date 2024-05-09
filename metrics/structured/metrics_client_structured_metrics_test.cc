// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metrics/structured/event_base.h"
#include "metrics/structured/metrics_client_structured_events.h"
#include "metrics/structured/mock_recorder.h"
#include "metrics/structured/recorder_singleton.h"
#include "metrics/structured/structured_events.h"

namespace metrics_client {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Return;

// Returns argc for a given argv. Just to avoid messing up the counts by doing
// it manually. Assumes argv has a nullptr as the last element!
template <int N>
int GetArgc(const char* const (&argv)[N]) {
  return N - 1;
}

constexpr char kFakeErrFile[] = "err.txt";
class SendStructuredMetricTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base::FilePath fake_err_file = temp_dir_.GetPath().Append(kFakeErrFile);
    fake_err_.reset(fopen(fake_err_file.value().c_str(), "w"));
    ASSERT_TRUE(fake_err_);
  }

  void TearDown() override {
    CHECK(added_mock_recorder_);
    metrics::structured::RecorderSingleton::GetInstance()
        ->DestroyRecorderForTest();
  }

  // Returns the "stderr" output of the SendStructuredMetric call, by reading in
  // the fake err file. NOTE: closes fake_err_.
  std::string GetOutput() {
    // Force fake_err_ to complete any pending writes by closing it.
    fake_err_.reset();
    base::FilePath fake_err_file = temp_dir_.GetPath().Append(kFakeErrFile);
    std::string contents;
    CHECK(base::ReadFileToString(fake_err_file, &contents));
    return contents;
  }

  // Expect that Recorder::Record is not called. NOTE: Can only be called once
  // per test.
  void ExpectNoRecordCall() {
    CHECK(!added_mock_recorder_);
    auto recorder = std::make_unique<metrics::structured::MockRecorder>();
    EXPECT_CALL(*recorder, Record(_)).Times(0);
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));
    added_mock_recorder_ = true;
  }

  // Expect that Recorder::Record is called once with the given event.
  // |return_value| will be the return value of the Record call. NOTE: Can only
  // be called once per test.
  void ExpectRecordCall(const metrics::structured::EventBase& event,
                        bool return_value = true) {
    CHECK(!added_mock_recorder_);
    auto recorder = std::make_unique<metrics::structured::MockRecorder>();
    EXPECT_CALL(*recorder, Record(event)).WillOnce(Return(return_value));
    metrics::structured::RecorderSingleton::GetInstance()->SetRecorderForTest(
        std::move(recorder));
    added_mock_recorder_ = true;
  }

  base::ScopedTempDir temp_dir_;

  // Used instead of stderr for SendStructuredMetric's err parameter. This
  // writes to a file inside temp_dir_ so the test can examine the output of
  // SendStructuredMetric.
  base::ScopedFILE fake_err_;

  bool added_mock_recorder_ = false;
};

TEST_F(SendStructuredMetricTest, FailsNoProjectName) {
  const char* const argv[] = {"metrics_client", "--structured", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(), HasSubstr("metrics client: missing project name\n"));
}

TEST_F(SendStructuredMetricTest, FailsNoEventName) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "TestProjectOne", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(), HasSubstr("metrics client: missing event name\n"));
}

TEST_F(SendStructuredMetricTest, FailsUnknownProjectName) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "UnknownProject", "TestEventOne", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Unknown project UnknownProject\n"));
}

TEST_F(SendStructuredMetricTest, FailsUnknownEventName) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "TestProjectOne", "UnknownEventName", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Unknown event UnknownEventName for "
                        "project TestProjectOne\n"));
}

TEST_F(SendStructuredMetricTest, WorksNoArgs) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "TestProjectOne", "TestEventOne", nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksOneArgWithEquals) {
  const char* const argv[] = {"metrics_client",     "--structured",
                              "TestProjectOne",     "TestEventOne",
                              "--TestMetricTwo=64", nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricTwo(64);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksOneArgWithSpace) {
  const char* const argv[] = {
      "metrics_client",  "--structured", "TestProjectOne", "TestEventOne",
      "--TestMetricTwo", "64",           nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricTwo(64);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksOneArgWithSingleDash) {
  const char* const argv[] = {
      "metrics_client", "--structured", "TestProjectOne", "TestEventOne",
      "-TestMetricTwo", "64",           nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricTwo(64);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, FailsArgNoDashes) {
  const char* const argv[] = {
      "metrics_client", "--structured", "TestProjectOne", "TestEventOne",
      "TestMetricTwo",  "64",           nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Unexpected arg TestMetricTwo\n"));
}

TEST_F(SendStructuredMetricTest, FailsArgNoValue) {
  const char* const argv[] = {"metrics_client",  "--structured",
                              "TestProjectOne",  "TestEventOne",
                              "--TestMetricTwo", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(
      GetOutput(),
      HasSubstr("metrics client: argument --TestMetricTwo has no value\n"));
}

TEST_F(SendStructuredMetricTest, FailsBadIntValue) {
  const char* const argv[] = {
      "metrics_client",  "--structured", "TestProjectOne", "TestEventOne",
      "--TestMetricTwo", "hello",        nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Cannot parse 'hello' as int\n"));
}

TEST_F(SendStructuredMetricTest, FailsBadIntValueWithDash) {
  const char* const argv[] = {"metrics_client",        "--structured",
                              "TestProjectOne",        "TestEventOne",
                              "--TestMetricTwo=hello", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Cannot parse 'hello' as int\n"));
}

TEST_F(SendStructuredMetricTest, FailsBadDoubleValue) {
  const char* const argv[] = {
      "metrics_client",    "--structured", "TestProjectOne", "TestEventOne",
      "--TestMetricThree", "hello",        nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Cannot parse 'hello' as double\n"));
}

TEST_F(SendStructuredMetricTest, FailsBadDoubleValueWithDash) {
  const char* const argv[] = {"metrics_client",          "--structured",
                              "TestProjectOne",          "TestEventOne",
                              "--TestMetricThree=hello", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Cannot parse 'hello' as double\n"));
}

TEST_F(SendStructuredMetricTest, FailsUnknownMetricName) {
  const char* const argv[] = {
      "metrics_client",  "--structured", "TestProjectOne", "TestEventOne",
      "--UnknownMetric", "hello",        nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Unknown metric name UnknownMetric\n"));
}

TEST_F(SendStructuredMetricTest, FailsUnknownMetricNameWithDash) {
  const char* const argv[] = {"metrics_client",        "--structured",
                              "TestProjectOne",        "TestEventOne",
                              "--UnknownMetric=hello", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Unknown metric name UnknownMetric\n"));
}

TEST_F(SendStructuredMetricTest, WorksOneArgEmptyStringEqualsSign) {
  const char* const argv[] = {"metrics_client",  "--structured",
                              "TestProjectOne",  "TestEventOne",
                              "-TestMetricOne=", nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("");
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksOneArgEmptyStringSpace) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "TestProjectOne", "TestEventOne",
                              "-TestMetricOne", "",
                              nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("");
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksMultipleArgs) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProjectOne",
                              "TestEventOne",
                              "--TestMetricOne",
                              "hello",
                              "--TestMetricTwo",
                              "64",
                              "--TestMetricThree",
                              "7.5",
                              nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("hello");
  event.SetTestMetricTwo(64);
  event.SetTestMetricThree(7.5);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, FailsMultipleArgsLastNoValue) {
  const char* const argv[] = {
      "metrics_client",  "--structured", "TestProjectOne",  "TestEventOne",
      "--TestMetricTwo", "64",           "--TestMetricOne", nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(
      GetOutput(),
      HasSubstr("metrics client: argument --TestMetricOne has no value\n"));
}

TEST_F(SendStructuredMetricTest, WorksMultipleArgsSomeEquals) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProjectOne",
                              "TestEventOne",
                              "--TestMetricOne=hello",
                              "--TestMetricTwo=64",
                              "--TestMetricThree",
                              "7.5",
                              nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("hello");
  event.SetTestMetricTwo(64);
  event.SetTestMetricThree(7.5);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksMultipleArgsAllEquals) {
  const char* const argv[] = {"metrics_client",        "--structured",
                              "TestProjectOne",        "TestEventOne",
                              "--TestMetricOne=hello", "--TestMetricTwo=64",
                              "--TestMetricThree=7.5", nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("hello");
  event.SetTestMetricTwo(64);
  event.SetTestMetricThree(7.5);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksMultipleArgsEmptyStringSpace) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProjectOne",
                              "TestEventOne",
                              "--TestMetricOne",
                              "",
                              "--TestMetricTwo",
                              "64",
                              "--TestMetricThree",
                              "7.5",
                              nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("");
  event.SetTestMetricTwo(64);
  event.SetTestMetricThree(7.5);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksMultipleArgsEmptyStringEqualsSign) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProjectOne",
                              "TestEventOne",
                              "--TestMetricOne=",
                              "--TestMetricTwo",
                              "64",
                              "--TestMetricThree",
                              "7.5",
                              nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  event.SetTestMetricOne("");
  event.SetTestMetricTwo(64);
  event.SetTestMetricThree(7.5);
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, FailsDuplicateArgument) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProjectOne",
                              "TestEventOne",
                              "--TestMetricOne=hello",
                              "--TestMetricTwo",
                              "64",
                              "--TestMetricOne=goodbye",
                              nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(
      GetOutput(),
      HasSubstr("metrics client: multiple --TestMetricOne arguments.\n"));
}

TEST_F(SendStructuredMetricTest, WorksForIntArraysWithEqualsSign) {
  const char* const argv[] = {"metrics_client",           "--structured",
                              "TestProjectThree",         "TestEventFour",
                              "--TestMetricFive=1,2,3,4", nullptr};
  metrics::structured::events::test_project_three::TestEventFour event;
  event.SetTestMetricFive({1, 2, 3, 4});
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksForEmptyIntArraysWithEqualsSign) {
  const char* const argv[] = {"metrics_client",    "--structured",
                              "TestProjectThree",  "TestEventFour",
                              "--TestMetricFive=", nullptr};
  metrics::structured::events::test_project_three::TestEventFour event;
  event.SetTestMetricFive({});
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksForIntArraysWithSpace) {
  const char* const argv[] = {
      "metrics_client", "--structured",     "TestProjectThree",
      "TestEventFour",  "--TestMetricFive", "1,2,3,4",
      nullptr};
  metrics::structured::events::test_project_three::TestEventFour event;
  event.SetTestMetricFive({1, 2, 3, 4});
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksForEmptyIntArraysWithSpace) {
  const char* const argv[] = {
      "metrics_client", "--structured",     "TestProjectThree",
      "TestEventFour",  "--TestMetricFive", "",
      nullptr};
  metrics::structured::events::test_project_three::TestEventFour event;
  event.SetTestMetricFive({});
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, WorksForIntArraysMaxElements) {
  const char* const argv[] = {
      "metrics_client", "--structured",     "TestProjectThree",
      "TestEventFour",  "--TestMetricFive", "1,2,3,4,5,6,7,8,9,10",
      nullptr};
  metrics::structured::events::test_project_three::TestEventFour event;
  event.SetTestMetricFive({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, FailsIntArrayTooManyElements) {
  const char* const argv[] = {
      "metrics_client", "--structured",     "TestProjectThree",
      "TestEventFour",  "--TestMetricFive", "1,2,3,4,5,6,7,8,9,10,11",
      nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Too many values for TestMetricFive "
                        "(got 11, maximum is 10)\n"));
}

TEST_F(SendStructuredMetricTest, FailsBadIntArrayValue) {
  const char* const argv[] = {
      "metrics_client", "--structured",     "TestProjectThree",
      "TestEventFour",  "--TestMetricFive", "1,q,3,4",
      nullptr};
  ExpectNoRecordCall();

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(
      GetOutput(),
      HasSubstr("metrics client: Cannot parse '1,q,3,4' as int-array\n"));
}

TEST_F(SendStructuredMetricTest, CommandLineUsesUnsantizedNames) {
  const char* const argv[] = {"metrics_client",
                              "--structured",
                              "TestProject.With.Name.Not.Legal.CPP",
                              "TestEvent.With.Name.Not.Legal.CPP",
                              "--TestMetric.With.Name.Not.Legal.CPP",
                              "hello",
                              nullptr};
  metrics::structured::events::test_project__with__name__not__legal_cpp::
      TestEvent_With_Name_Not_Legal_CPP event;
  event.SetTestMetric_With_Name_Not_Legal_CPP("hello");
  ExpectRecordCall(event);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_SUCCESS);
  EXPECT_THAT(GetOutput(), IsEmpty());
}

TEST_F(SendStructuredMetricTest, FailsIfRecordFails) {
  const char* const argv[] = {"metrics_client", "--structured",
                              "TestProjectOne", "TestEventOne", nullptr};
  metrics::structured::events::test_project_one::TestEventOne event;
  ExpectRecordCall(event, false);

  EXPECT_EQ(SendStructuredMetric(GetArgc(argv), argv, 2, fake_err_.get()),
            EXIT_FAILURE);
  EXPECT_THAT(GetOutput(),
              HasSubstr("metrics client: Event recording failed.\n"));
}

}  // namespace
}  // namespace metrics_client
