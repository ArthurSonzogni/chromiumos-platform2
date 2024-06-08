// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "pmt_tool/pmt_tool.h"
#include "pmt_tool/utils.h"

using pmt_tool::Format;
using pmt_tool::Formatter;
using pmt_tool::Options;
using pmt_tool::Source;
using testing::Ref;
using testing::Return;

class SourceMock : public Source {
 public:
  MOCK_METHOD(std::optional<const pmt::Snapshot*>, TakeSnapshot, ());
  MOCK_METHOD(size_t, GetSnapshotSize, ());
  MOCK_METHOD(bool, SetUp, (const Options& options));
  MOCK_METHOD(void, Sleep, (uint64_t interval));
};

class FormatterMock : public Formatter {
 public:
  MOCK_METHOD(bool, SetUp, (const Options& opts, int fd, size_t snapshot_size));
  MOCK_METHOD(void, Format, (const pmt::Snapshot& snapshot));
};

class pmtToolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // The base::CommandLine is initialized at the start of each test. So ensure
    // we get a clean start since ParseCommandLineAndInitLogging() uses both
    // brillo::FlagHelper and the underlying base::CommandLine.
    if (base::CommandLine::InitializedForCurrentProcess())
      base::CommandLine::Reset();
    brillo::FlagHelper::ResetForTesting();
  }
};

TEST_F(pmtToolTest, ParseCmdlineDefaults) {
  Options opts;
  const char* argv[] = {"pmt_tool"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us, 1 * base::Time::kMicrosecondsPerSecond);
  EXPECT_EQ(opts.sampling.duration_samples, 0);
  EXPECT_EQ(opts.sampling.duration_seconds, 0);
  EXPECT_TRUE(opts.sampling.input_file.empty());
  EXPECT_EQ(opts.decoding.format, Format::RAW);
}

TEST_F(pmtToolTest, ParseCmdlineInterval) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-i=123",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us,
            123 * base::Time::kMicrosecondsPerSecond);
}

TEST_F(pmtToolTest, ParseCmdlineIntervalFractional) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-i=123.321",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us, 123321000);
}

TEST_F(pmtToolTest, ParseCmdlineIntervalZero) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-i=0",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us, 0);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSamples) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-n=10"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.duration_samples, 10);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSamplesNegative) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-n=-1"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_FALSE(result);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSamplesZero) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-n=0"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.duration_samples, 0);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSeconds) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-i=2.5", "-t=10"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us, 2500000);
  EXPECT_EQ(opts.sampling.duration_seconds, 10);
  // When time-sampling, we always sample at the start and then every -i.
  EXPECT_EQ(opts.sampling.duration_samples, 5);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSecondsRounding) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-i=2.5", "-t=9"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.interval_us, 2500000);
  EXPECT_EQ(opts.sampling.duration_seconds, 10);
  // When time-sampling, we always sample at the start and then every -i.
  EXPECT_EQ(opts.sampling.duration_samples, 5);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSecondsZero) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-t=0"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.duration_seconds, 0);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSecondsAndSamples) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-t=2", "-n=10"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_FALSE(result);
}

TEST_F(pmtToolTest, ParseCmdlineDurationSecondsTooHighInterval) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-i=10", "-t=5"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_FALSE(result);
}

TEST_F(pmtToolTest, ParseCmdlineFormatCSV) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-f=csv",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.decoding.format, Format::CSV);
}

TEST_F(pmtToolTest, ParseCmdlineFormatRAW) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-f=raw",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.decoding.format, Format::RAW);
}

TEST_F(pmtToolTest, ParseCmdlineFormatDBG) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-f=dbg",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.decoding.format, Format::DBG);
}

TEST_F(pmtToolTest, ParseCmdlineFormatInvalid) {
  Options opts;
  const char* argv[] = {
      "pmt_tool",
      "-f=foobar",
  };
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_FALSE(result);
}

TEST_F(pmtToolTest, ParseCmdlineFile) {
  Options opts;
  auto file = base::FilePath("/dev/null");
  auto file_str = file.MaybeAsASCII();
  const char* argv[] = {"pmt_tool", "-i=1", "--", file_str.c_str()};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(opts.sampling.input_file, file);
}

TEST_F(pmtToolTest, ParseCmdlineFileInvalid) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-i=1", "--", "/nonexistent/path"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_FALSE(result);
}

TEST_F(pmtToolTest, ParseCmdlineVerbose) {
  Options opts;
  const char* argv[] = {"pmt_tool", "-v=1"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_TRUE(VLOG_IS_ON(PMT_TOOL_LOG_DBG));
}

TEST_F(pmtToolTest, ParseCmdlineNonVerbose) {
  Options opts;
  const char* argv[] = {"pmt_tool"};
  bool cl_initialized = base::CommandLine::Init(std::size(argv), argv);
  ASSERT_TRUE(cl_initialized);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      base::CommandLine::ForCurrentProcess());
  auto result = ParseCommandLineAndInitLogging(std::size(argv), argv, opts);

  EXPECT_TRUE(result);
  EXPECT_FALSE(VLOG_IS_ON(PMT_TOOL_LOG_DBG));
}

TEST_F(pmtToolTest, RunSourceSetupFailed) {
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 1,
              .duration_samples = 1,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(false));
  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 2);
}

TEST_F(pmtToolTest, RunFormatterSetupFailed) {
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 1,
              .duration_samples = 1,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(false));
  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 3);
}

TEST_F(pmtToolTest, Run1SampleWithInterval) {
  pmt::Snapshot fake_snapshot;
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 12,
              .duration_samples = 1,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(true));
  EXPECT_CALL(source_mock, TakeSnapshot).WillOnce(Return(&fake_snapshot));
  EXPECT_CALL(formatter_mock, Format(Ref(fake_snapshot)));

  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 0);
}

TEST_F(pmtToolTest, Run10SamplesWithInterval) {
  pmt::Snapshot fake_snapshot;
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 12,
              .duration_samples = 10,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(true));
  EXPECT_CALL(source_mock, TakeSnapshot)
      .Times(opts.sampling.duration_samples)
      .WillRepeatedly(Return(&fake_snapshot));
  EXPECT_CALL(formatter_mock, Format(Ref(fake_snapshot)))
      .Times(opts.sampling.duration_samples);
  EXPECT_CALL(source_mock, Sleep(opts.sampling.interval_us))
      .Times(opts.sampling.duration_samples - 1);

  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 0);
}

TEST_F(pmtToolTest, RunInDumpMode) {
  pmt::Snapshot fake_snapshot;
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 123,
              .duration_samples = 0,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(true));
  // do_run() will do the sampling+formatting loop until source returns nullptr.
  EXPECT_CALL(source_mock, TakeSnapshot)
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(std::optional<const pmt::Snapshot*>()));
  EXPECT_CALL(source_mock, Sleep(opts.sampling.interval_us)).Times(3);
  EXPECT_CALL(formatter_mock, Format(Ref(fake_snapshot))).Times(3);

  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 0);
}

TEST_F(pmtToolTest, RunInDumpModeNoSleep) {
  pmt::Snapshot fake_snapshot;
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 0,
              .duration_samples = 0,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(true));
  // do_run() will do the sampling+formatting loop until source returns nullptr.
  EXPECT_CALL(source_mock, TakeSnapshot)
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(&fake_snapshot))
      .WillOnce(Return(std::optional<const pmt::Snapshot*>()));
  EXPECT_CALL(formatter_mock, Format(Ref(fake_snapshot))).Times(3);

  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 0);
}

TEST_F(pmtToolTest, RunInDumpModeLimitedSamples) {
  pmt::Snapshot fake_snapshot;
  SourceMock source_mock;
  FormatterMock formatter_mock;
  Options opts = {
      .sampling =
          {
              .interval_us = 0,
              .duration_samples = 5,
          },
  };
  EXPECT_CALL(source_mock, SetUp).WillOnce(Return(true));
  EXPECT_CALL(source_mock, GetSnapshotSize).WillOnce(Return(123));
  EXPECT_CALL(formatter_mock, SetUp(Ref(opts), STDOUT_FILENO, 123))
      .WillOnce(Return(true));
  EXPECT_CALL(source_mock, TakeSnapshot)
      .Times(opts.sampling.duration_samples)
      .WillRepeatedly(Return(&fake_snapshot));
  EXPECT_CALL(formatter_mock, Format(Ref(fake_snapshot)))
      .Times(opts.sampling.duration_samples);

  int result = do_run(opts, source_mock, formatter_mock);
  ASSERT_EQ(result, 0);
}
