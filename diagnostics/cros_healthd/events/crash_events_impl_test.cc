// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/crash_events.h"

#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/strings/string_piece.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;
using ::testing::HasSubstr;

constexpr uint64_t kInitOffset = 100;

constexpr char kValidLogLine[] =
    R"TEXT({"path_hash":"a_local_id","capture_time":"9876543",)TEXT"
    R"TEXT("fatal_crash_type":"kernel","upload_id":"a_crash_report_id"})TEXT";

const auto kExpectedUnuploadedResultForValidLogLine =
    mojom::CrashEventInfo::New(
        /*crash_type=*/mojom::CrashEventInfo::CrashType::kKernel,
        /*local_id=*/"a_local_id",
        /*capture_time=*/base::Time::FromDoubleT(9876543.0),
        /*upload_info=*/nullptr);

const auto kExpectedUploadedResultForValidLogLine = mojom::CrashEventInfo::New(
    /*crash_type=*/mojom::CrashEventInfo::CrashType::kKernel,
    /*local_id=*/"a_local_id",
    /*capture_time=*/base::Time::FromDoubleT(9876543.0),
    /*upload_info=*/
    mojom::CrashUploadInfo::New(
        /*crash_report_id=*/"a_crash_report_id",
        /*creation_time=*/base::Time(),
        /*offset=*/0u));

constexpr char kInvalidLogLine[] = "{{{{";

// Tests parsing each valid field outside upload_info.
class UploadsLogParserValidFieldTest : public ::testing::TestWithParam<bool> {
 protected:
  bool is_uploaded() { return GetParam(); }
};

TEST_P(UploadsLogParserValidFieldTest, ParseLocalID) {
  const auto result = ParseUploadsLog(
      R"TEXT({"path_hash":"some_hash0","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT"
      "\n"
      R"TEXT({"path_hash":"some_hash1","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT",
      /*is_uploaded=*/is_uploaded(),
      /*creation_time=*/base::Time(),
      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->local_id, "some_hash0");
  EXPECT_EQ(result[1]->local_id, "some_hash1");
}

TEST_P(UploadsLogParserValidFieldTest, ParseCaptureTime) {
  const auto result = ParseUploadsLog(
      R"TEXT({"path_hash":"some_hash","capture_time":"10",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT"
      "\n"
      R"TEXT({"path_hash":"some_hash","capture_time":"100",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT",
      /*is_uploaded=*/is_uploaded(),
      /*creation_time=*/base::Time(),
      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->capture_time, base::Time::FromDoubleT(10.0));
  EXPECT_EQ(result[1]->capture_time, base::Time::FromDoubleT(100.0));
}

TEST_P(UploadsLogParserValidFieldTest, ParseCrashType) {
  const auto result = ParseUploadsLog(
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT"
      "\n"
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"ec","upload_id":"abc"})TEXT"
      "\n"
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"some_unknown_value","upload_id":"abc"})TEXT"
      "\n"
      // fatal_crash_type is missing
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("upload_id":"abc"})TEXT",
      /*is_uploaded=*/is_uploaded(),
      /*creation_time=*/base::Time(),
      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 4u);
  EXPECT_EQ(result[0]->crash_type, mojom::CrashEventInfo::CrashType::kKernel);
  EXPECT_EQ(result[1]->crash_type,
            mojom::CrashEventInfo::CrashType::kEmbeddedController);
  EXPECT_EQ(result[2]->crash_type, mojom::CrashEventInfo::CrashType::kUnknown);
  EXPECT_EQ(result[3]->crash_type, mojom::CrashEventInfo::CrashType::kUnknown);
}

INSTANTIATE_TEST_SUITE_P(VaryingIsUploaded,
                         UploadsLogParserValidFieldTest,
                         testing::Bool(),
                         [](const ::testing::TestParamInfo<
                             UploadsLogParserValidFieldTest::ParamType>& info) {
                           return info.param ? "uploaded" : "unuploaded";
                         });

// Tests fields inside upload_info

TEST(UploadsLogParserTest, ParseValidUnuploaded) {
  const auto result = ParseUploadsLog(
      // With upload_id
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT"
      "\n"
      // Missing upload_id
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel"})TEXT",
      /*is_uploaded=*/false,
      /*creation_time=*/base::Time(),
      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_TRUE(result[0]->upload_info.is_null());
  EXPECT_TRUE(result[1]->upload_info.is_null());
}

TEST(UploadsLogParserTest, ParseUploadedValidCrashReportID) {
  const auto result = ParseUploadsLog(
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"abc"})TEXT"
      "\n"
      R"TEXT({"path_hash":"some_hash","capture_time":"2",)TEXT"
      R"TEXT("fatal_crash_type":"kernel","upload_id":"de"})TEXT",
      /*is_uploaded=*/true,
      /*creation_time=*/base::Time(),
      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->upload_info->crash_report_id, "abc");
  EXPECT_EQ(result[1]->upload_info->crash_report_id, "de");
}

TEST(UploadsLogParserTest, ParseOffsetWithValidLinesOnly) {
  std::ostringstream stream;
  stream << kValidLogLine << "\n";
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/kInitOffset);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->upload_info->offset, kInitOffset + 0u);
  EXPECT_EQ(result[1]->upload_info->offset, kInitOffset + 1u);
}

TEST(UploadsLogParserTest, ParseOffsetWithInvalidLine) {
  std::ostringstream stream;
  stream << kValidLogLine << "\n";
  stream << kInvalidLogLine << "\n";
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/kInitOffset);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->upload_info->offset, kInitOffset + 0u);
  EXPECT_EQ(result[1]->upload_info->offset, kInitOffset + 1u);
}

TEST(UploadsLogParserTest, ParseOffsetWithBlankLine) {
  std::ostringstream stream;
  stream << kValidLogLine << "\n";
  stream << "\n";  // blank line
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/kInitOffset);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->upload_info->offset, kInitOffset + 0u);
  EXPECT_EQ(result[1]->upload_info->offset, kInitOffset + 1u);
}

TEST(UploadsLogParserTest, PassThroughCreationTime) {
  static constexpr base::Time kCreationTime = base::Time::FromTimeT(300);
  std::ostringstream stream;
  stream << kValidLogLine << "\n";
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/kCreationTime,
                                      /*init_offset=*/kInitOffset);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0]->upload_info->creation_time, kCreationTime);
  EXPECT_EQ(result[1]->upload_info->creation_time, kCreationTime);
}

TEST(UploadsLogParserTest, MultipleDelimitersLogLineBreaksCorrectly) {
  constexpr char kWhitespaces[] = {' ', '\n', '\t', '\r', '\f'};
  std::ostringstream stream;
  for (const auto delimiter : kWhitespaces) {
    stream << kValidLogLine << delimiter;
  }
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/kInitOffset);
  EXPECT_EQ(result.size(), std::size(kWhitespaces));
}

// Tests invalid or blank lines.
class UploadsLogParserInvalidTest
    : public ::testing::TestWithParam<
          std::tuple<std::string, std::string, std::string>> {
 protected:
  void SetUp() override { brillo::LogToString(true); }
  void TearDown() override { brillo::LogToString(false); }

  const std::string& invalid_log_line() { return std::get<1>(GetParam()); }

  const std::string& expected_log_string() { return std::get<2>(GetParam()); }
};

TEST_P(UploadsLogParserInvalidTest, ParseOneInvalid) {
  brillo::ClearLog();
  const auto result = ParseUploadsLog(invalid_log_line(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/0u);
  EXPECT_THAT(brillo::GetLog(), HasSubstr(expected_log_string()))
      << "Log does not contain target string: " << brillo::GetLog();
  EXPECT_EQ(result.size(), 0u);
}

TEST_P(UploadsLogParserInvalidTest, ParseOneInvalidFollowingOneValid) {
  std::stringstream stream;
  stream << kValidLogLine << '\n';
  stream << invalid_log_line();
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/true,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_TRUE(result[0].Equals(kExpectedUploadedResultForValidLogLine));
}

INSTANTIATE_TEST_SUITE_P(
    VaryingInvalidLines,
    UploadsLogParserInvalidTest,
    testing::ValuesIn(std::vector<
                      std::tuple<std::string, std::string, std::string>>{
        {"InvalidJSON", "{", "Invalid JSON in crash uploads log"},
        {"MissingLocalID", R"TEXT({"capture_time":"2","upload_id":"abc"})TEXT",
         "Local ID not found"},
        {"MissingCaptureTime",
         R"TEXT({"path_hash":"some_hash","upload_id":"abc"})TEXT",
         "Capture time not found"},
        {"InvalidCaptureTime",
         R"T({"path_hash":"some_hash","upload_id":"abc","capture_time":"ab"})T",
         "Invalid capture time"},
        {"MissingCrashReportIDWithUploaded",
         R"TEXT({"capture_time":"2","path_hash":"some_hash"})TEXT",
         "Crash report ID is not found while the crash has been uploaded"},
        {"BlankLine", "", ""}}),
    [](const ::testing::TestParamInfo<UploadsLogParserInvalidTest::ParamType>&
           info) { return std::get<0>(info.param); });

// Tests valid lines when there are invalid lines. Focuses on varying the
// relative locations of valid lines and invalid lines. The relativity of the
// locations are lightly tested but are helpful in case there are bugs caused by
// the change in the line-by-line nature of the parser in the future.

TEST(UploadsLogParserTest, ParseTwoSeparateValidLines) {
  std::ostringstream stream;
  stream << kValidLogLine << "\n";
  stream << kInvalidLogLine << "\n";
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/false,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_TRUE(result[0].Equals(kExpectedUnuploadedResultForValidLogLine));
  EXPECT_TRUE(result[1].Equals(kExpectedUnuploadedResultForValidLogLine));
}

TEST(UploadsLogParserTest, ParseTwoTrailingValidLinesWithBlank) {
  std::ostringstream stream;
  stream << kInvalidLogLine << "\n";
  stream << "\n";  // Blank line
  stream << kValidLogLine << "\n";
  stream << kValidLogLine << "\n";
  const auto result = ParseUploadsLog(stream.str(), /*is_uploaded=*/false,
                                      /*creation_time=*/base::Time(),
                                      /*init_offset=*/0u);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_TRUE(result[0].Equals(kExpectedUnuploadedResultForValidLogLine));
  EXPECT_TRUE(result[1].Equals(kExpectedUnuploadedResultForValidLogLine));
}

}  // namespace
}  // namespace diagnostics
