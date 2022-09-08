// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/vpd_reader/vpd_reader_impl.h"

#include <optional>
#include <string>

#include <base/logging.h>
#include <brillo/process/process_mock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_foundation {

namespace {

using ::testing::Return;
using ::testing::StrictMock;

const char kFakeVpdPath[] = "/fake/pvd/path";
const char kVpdListOption[] = "-l";

// Prefer to call `GetFakeVpdOutput()` since the extra newline at head.
constexpr char kFakeVpdOutput[] = R"(
"ABC"="DEF"
"JFK"="XYZ"
"QQQ"="""""
"EEE"="==="
)";

// Returns the string formatted as `vpd -l` would dump.
const std::string GetFakeVpdOutput() {
  // Offset by 1 to skip the first newline.
  return std::string(kFakeVpdOutput + 1);
}

}  // namespace

class VpdReaderImplTest : public ::testing::Test {
 public:
  VpdReaderImplTest() {
    ON_CALL(*process_mock_, GetOutputString(STDOUT_FILENO))
        .WillByDefault(Return(fake_output_));
  }

 protected:
  // The mock process that is owned and used by the object under test later.
  StrictMock<brillo::ProcessMock>* process_mock_ =
      new StrictMock<brillo::ProcessMock>();
  std::string fake_output_ = GetFakeVpdOutput();
};

namespace {

TEST_F(VpdReaderImplTest, GetSuccess) {
  EXPECT_CALL(*process_mock_, AddArg(kFakeVpdPath));
  EXPECT_CALL(*process_mock_, AddArg(kVpdListOption));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDOUT_FILENO));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDERR_FILENO));
  EXPECT_CALL(*process_mock_, Run()).WillOnce(Return(0));
  EXPECT_CALL(*process_mock_, GetOutputString(STDOUT_FILENO));
  VpdReaderImpl reader(std::unique_ptr<brillo::Process>(process_mock_),
                       kFakeVpdPath);
  std::optional<std::string> output = reader.Get("ABC");
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output.value(), "DEF");
  // Query another entry should not invoke the vpd process again; the strict
  // mock will verify.
  output = reader.Get("JFK");
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output.value(), "XYZ");
  // The value with double quotes should be supported.
  output = reader.Get("QQQ");
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output.value(), "\"\"\"");
  // The value with '=' quoshould be supported.
  output = reader.Get("EEE");
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output.value(), "===");
}

TEST_F(VpdReaderImplTest, GetFailureAbsentKey) {
  EXPECT_CALL(*process_mock_, AddArg(kFakeVpdPath));
  EXPECT_CALL(*process_mock_, AddArg(kVpdListOption));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDOUT_FILENO));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDERR_FILENO));
  EXPECT_CALL(*process_mock_, Run()).WillOnce(Return(0));
  EXPECT_CALL(*process_mock_, GetOutputString(STDOUT_FILENO));
  VpdReaderImpl reader(std::unique_ptr<brillo::Process>(process_mock_),
                       kFakeVpdPath);
  std::optional<std::string> output = reader.Get("a non-existent key");
  EXPECT_FALSE(output.has_value());
}

TEST_F(VpdReaderImplTest, GetFailureKeyValueFormatError) {
  EXPECT_CALL(*process_mock_, AddArg(kFakeVpdPath));
  EXPECT_CALL(*process_mock_, AddArg(kVpdListOption));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDOUT_FILENO));
  EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDERR_FILENO));
  EXPECT_CALL(*process_mock_, Run()).WillOnce(Return(0));

  fake_output_.erase(fake_output_.find('='), 1);
  EXPECT_CALL(*process_mock_, GetOutputString(STDOUT_FILENO))
      .WillOnce(Return(fake_output_));

  VpdReaderImpl reader(std::unique_ptr<brillo::Process>(process_mock_),
                       kFakeVpdPath);
  std::optional<std::string> output = reader.Get("ABC");
  EXPECT_FALSE(output.has_value());
}

TEST_F(VpdReaderImplTest, GetFailureMissingExpectedDoubleQuote) {
  size_t double_quote_pos = 0;
  for (int i = 0; i < 4; ++i) {
    // Make the number of iteration verbose for debugging.
    LOG(INFO) << "Removing \"; iteration " << i;
    EXPECT_CALL(*process_mock_, AddArg(kFakeVpdPath));
    EXPECT_CALL(*process_mock_, AddArg(kVpdListOption));
    EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDOUT_FILENO));
    EXPECT_CALL(*process_mock_, RedirectUsingMemory(STDERR_FILENO));
    EXPECT_CALL(*process_mock_, Run()).WillOnce(Return(0));

    fake_output_ = GetFakeVpdOutput();
    double_quote_pos = fake_output_.find('\"', double_quote_pos);
    fake_output_.erase(double_quote_pos, 1);
    ++double_quote_pos;
    EXPECT_CALL(*process_mock_, GetOutputString(STDOUT_FILENO))
        .WillOnce(Return(fake_output_));

    VpdReaderImpl reader(std::unique_ptr<brillo::Process>(process_mock_),
                         kFakeVpdPath);
    std::optional<std::string> output = reader.Get("ABC");
    EXPECT_FALSE(output.has_value());
    // AS a workaround, re-create the process mock.
    process_mock_ = new StrictMock<brillo::ProcessMock>();
  }
}

}  // namespace

}  // namespace hwsec_foundation
