// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector_text_file_reader.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <featured/fake_platform_features.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

namespace {
std::string ParamStringName(const testing::TestParamInfo<bool>& info) {
  return info.param ? "gravedigger" : "base_file";
}
}  // namespace

namespace anomaly {

class AnomalyDetectorFileReaderTest : public ::testing::TestWithParam<bool> {
 protected:
  using FileReaderRun = std::vector<std::string>;

  std::unique_ptr<TextFileReader> InitializeFileReaderForTest(
      const std::string& input_file_name) {
    base::FilePath input_file_path =
        test_util::GetTestDataPath(input_file_name, /*use_testdata=*/true);

    return std::make_unique<TextFileReader>(input_file_path,
                                            SetupFakeFeatureLibrary());
  }

  feature::PlatformFeaturesInterface* SetupFakeFeatureLibrary() {
    feature_library_ = std::make_unique<feature::FakePlatformFeatures>(nullptr);
    feature_library_->SetEnabled("CrOSLateBootGravedigger", GetParam());
    return feature_library_.get();
  }

  void ReaderTest(const std::unique_ptr<TextFileReader>& r,
                  const FileReaderRun& want) {
    FileReaderRun got{};
    std::string line;
    while (r->GetLine(&line)) {
      got.push_back(line);
    }
    ASSERT_EQ(want.size(), got.size());

    for (int i = 0; i < want.size(); i++) {
      EXPECT_EQ(want[i], got[i]);
    }
  }

  static bool HaveOpenLogFile(const TextFileReader* r) {
    if (!r) {
      return false;
    }
    return r->HaveOpenLogFile();
  }

  std::unique_ptr<feature::FakePlatformFeatures> feature_library_;
};

// Tests that an invalid file does not cause the entire program to fail.
TEST_P(AnomalyDetectorFileReaderTest, InvalidFileTest) {
  auto r = InitializeFileReaderForTest("FILE_DOES_NOT_EXIST");
  EXPECT_FALSE(HaveOpenLogFile(r.get()));

  // Make sure all public methods are safe with invalid file.
  std::string line;
  EXPECT_FALSE(r->GetLine(&line));
  r->SeekToEnd();
  r->SeekToBegin();
  SUCCEED();
}

TEST_P(AnomalyDetectorFileReaderTest, OpenFileTest) {
  auto r = InitializeFileReaderForTest("TEST_MESSAGE_LOG");
  EXPECT_TRUE(HaveOpenLogFile(r.get()));
}

// Tests if SeekToEnd() successfully moves the TextFileReader past
// the last line of the log to avoid re-reading of old logs.
TEST_P(AnomalyDetectorFileReaderTest, SeekToEndTest) {
  auto r = InitializeFileReaderForTest("TEST_MESSAGE_LOG");
  r->SeekToEnd();
  FileReaderRun want{};
  ReaderTest(r, want);
}

TEST_P(AnomalyDetectorFileReaderTest, FileTextReaderTest) {
  auto r = InitializeFileReaderForTest("TEST_MESSAGE_LOG");
  std::string l1 =
      "2020-05-10T22:45:04.419261Z ERR tpm_managerd[790]: TPM error "
      "0x3011 (Communication failure): Failed to connect context.";
  std::string l2 = "";
  std::string l3 =
      R"(2020-05-12T20:56:03.754453Z INFO rsyslogd[642]:  [origin )"
      R"(software="rsyslogd" swVersion="8.1904.0" x-pid="642" )"
      R"(x-info="https://www.rsyslog.com"] rsyslogd was HUPed)";
  std::string l4 =
      "2020-05-13T11:56:27.236308Z INFO kernel: [  893.009245] "
      "atme1_mxt_ts 3-004b: Status: 00 Config Checksum: 673e89";
  std::string l5 =
      "2020-05-14T19:37:04.202906Z INFO VM(3)[8947]:  "
      "[devices/src/virtio/balloon.rs:290] ballon config changed to consume "
      "255836 pages";
  std::string l6 =
      "2020-06-08T08:28:49.080656Z NOTICE [2917]: log message with no "
      "tag.";
  FileReaderRun want{std::move(l1), std::move(l2), std::move(l3),
                     std::move(l4), std::move(l5), std::move(l6)};
  ReaderTest(r, want);
}

// This test fixture is responsible for the creation and deletion of temporary
// files.
class AnomalyDetectorFileReaderConcurrentTest
    : public AnomalyDetectorFileReaderTest {
 protected:
  void SetUp() override {
    CHECK(base::CreateTemporaryFile(&path_));
    file_ = base::File(path_, base::File::FLAG_OPEN | base::File::FLAG_APPEND);
    CHECK(file_.IsValid());
  }

  void TearDown() override {
    CHECK(base::DeleteFile(path_));
    CHECK(base::DeleteFile(path_.AddExtension("old")));
  };

  // Use this method to append lines to file_.
  void AppendToFile(const std::vector<std::string>& lines) {
    for (auto line : lines) {
      file_.WriteAtCurrentPos(line.c_str(), line.length());
      file_.WriteAtCurrentPos("\n", 1);
    }
  }

  // This simulates log rotation. The original file_ will be moved to
  // ${path_}.old and a new file is created in its place. After this function
  // is called, AppendToFile will write to the newly createcd file.
  void RotateFiles() {
    CHECK(base::Move(path_, path_.AddExtension("old")));
    file_ =
        base::File(path_, base::File::FLAG_CREATE | base::File::FLAG_APPEND);
    CHECK(file_.IsValid());
  }

  std::unique_ptr<TextFileReader> MakeTextFileReader(
      std::string_view extension = "") {
    return std::make_unique<TextFileReader>(path_.AddExtension(extension),
                                            SetupFakeFeatureLibrary());
  }

  base::FilePath path_;
  base::File file_;
};

TEST_P(AnomalyDetectorFileReaderConcurrentTest, ReadAppendedTextTest) {
  std::vector<std::string> file_content{"line 1", "line 2", "end"};

  auto r = MakeTextFileReader();
  EXPECT_TRUE(HaveOpenLogFile(r.get()));

  FileReaderRun want{};
  // Test that the file is empty.
  ReaderTest(r, want);

  // Append new lines to the file.
  AppendToFile(file_content);

  want.insert(want.end(), file_content.begin(), file_content.end());
  // Test if the newly appended lines are correctly read by TextFileReader.
  ReaderTest(r, want);
}

// Test that text appended to file after calling SeekToEnd is read properly.
// This is important since LogReader calls SeekToEnd upon initialisation.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, ReadAfterSeekToEndTest) {
  std::vector<std::string> file_content{"line 1", "line 2", "end"};

  AppendToFile(file_content);

  auto r = MakeTextFileReader();
  r->SeekToEnd();

  AppendToFile(file_content);

  FileReaderRun want{};
  want.insert(want.end(), file_content.begin(), file_content.end());
  ReaderTest(r, want);
}

// This simulates concurrent read and write on the file.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, ReadDuringWriteTest) {
  auto r = MakeTextFileReader();

  file_.WriteAtCurrentPos("li", 2);
  // r will not return any line since there is no '\n' at the end.
  ReaderTest(r, {});

  file_.WriteAtCurrentPos("ne\n", 3);
  ReaderTest(r, {"line"});
}

// Tests if LoadToBuffer works as intended when given a file containing lines
// that are longer than the buffer size of TextFileReader.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, ReadLineLongerThanBufferTest) {
  auto r = MakeTextFileReader();

  const int size_larger_than_buffer = TextFileReader::kBufferSize_ + 1;

  // Create a line longer than TextFileReader::kBufferSize_.
  std::string long_line(size_larger_than_buffer, 'a');

  for (int i = 0; i < 2; i++) {
    file_.WriteAtCurrentPos(long_line.c_str(), size_larger_than_buffer);
    file_.WriteAtCurrentPos("\n", 1);
  }

  ReaderTest(r, {long_line, long_line});
}

// Test if TextFileReader can open a file that did not exist when it was
// initialised.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, OpenFileRetryTest) {
  // File <path_>.old does not exist yet.
  auto r = MakeTextFileReader("old");

  std::string line;
  EXPECT_FALSE(r->GetLine(&line));
  EXPECT_FALSE(HaveOpenLogFile(r.get()));

  // Write to file <path_>.
  std::vector<std::string> file_content{"line 1", "line 2", "end"};
  AppendToFile(file_content);

  // This moves the file <path_> to <path_>.old.
  RotateFiles();

  // TextFileReader should detect that <path_>.old now exists and read from it.
  FileReaderRun want{};
  want.insert(want.end(), file_content.begin(), file_content.end());
  ReaderTest(r, want);
}

// Test if TextFileReader stops retrying to open the file pointed by file_path_
// after kMaxOpenRetries_ times.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, OpenFileRetryExceededTest) {
  // File <path_>.old does not exist yet.
  auto r = MakeTextFileReader("old");

  // GetLine internally calls Open.
  std::string line;
  for (int i = 0; i < TextFileReader::kMaxOpenRetries_; i++) {
    r->GetLine(&line);
  }

  std::vector<std::string> file_content{"line 1", "line 2", "end"};
  AppendToFile(file_content);

  // This moves the file <path_> to <path_>.old. Now <path_>.old exists and has
  // content.
  RotateFiles();

  // r does not try to open <path_>.old anymore since maximum number of retries
  // have been reached.
  FileReaderRun want{};
  ReaderTest(r, want);
}

// The original file opened by TextFileReader is replaced by a new file.
// TextFileReader should finish reading the original file and then read the new
// file to the end.
TEST_P(AnomalyDetectorFileReaderConcurrentTest, HandleFileMoveTest) {
  std::vector<std::string> file_content_1{"line1", "line2", "end"};
  std::vector<std::string> file_content_2{"new line 1", "new line 2",
                                          "new end"};

  auto r = MakeTextFileReader();
  EXPECT_TRUE(HaveOpenLogFile(r.get()));

  // Adding lines to file_.
  AppendToFile(file_content_1);

  RotateFiles();
  // The original file is now moved to ${path_}.old but the internal files are
  // still pointing to it.

  // Adding lines to newly created file ${path_}.
  AppendToFile(file_content_2);

  // want should include both file_content_1 and file_content_2.
  FileReaderRun want{};
  want.insert(want.end(), file_content_1.begin(), file_content_1.end());
  want.insert(want.end(), file_content_2.begin(), file_content_2.end());

  // r should first read lines till the end of ${path_}.old file then open
  // ${path_} and read the lines from the new file.
  ReaderTest(r, want);
}

INSTANTIATE_TEST_SUITE_P(P,
                         AnomalyDetectorFileReaderTest,
                         testing::Bool(),
                         ParamStringName);
INSTANTIATE_TEST_SUITE_P(P,
                         AnomalyDetectorFileReaderConcurrentTest,
                         testing::Bool(),
                         ParamStringName);

}  // namespace anomaly
