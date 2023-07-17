// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/gsc_collector_base.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using base::FilePath;
using brillo::FindLog;
using brillo::ProcessImpl;

using ::testing::_;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {

// Sample GSC (Ti50 crash log (clog).
constexpr char kGscCrashLog[] =
    "0b000000000000000df0ad0b00000000000000000000000000000000020000001800000005"
    "00000050710b000b00008000000000000000006b65726e656c0000\n"
    "0df0ad0b00000000080000000300000048d70000000000000df0ad0b000000000c00000004"
    "00000050710b000b00008000000000000000000df0ad0b00000000\n"
    "4400000000000000e4220c00e4220c0078560c00f8e2010024eb010098ec0100e0f0010000"
    "0000000001000000000000010000000000000018c9090001000000\n"
    "480000004001000040020000000000000df0ad0b000000000a0000000000000066775f7570"
    "646174657200000000000094000000010000000100000088000000\n"
    "72763569f0470c000800000000000000b4470c000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "000000000000000000000000000000000df0ad0b00000000440000000000000094560c0094"
    "560c0028aa0d0058f1010050b9020020ba020008bf020000000000\n"
    "0001000000000000010000000000000018c909000100000048000000400100004002000000"
    "0000000df0ad0b00000000040000000000000074706d3200000000\n"
    "9400000001000000010000008800000072763569f6600c0008000000000000008e600c0000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000df0ad0b00"
    "000000440000000000000048aa0d0048aa0d0050850e0080bf0200\n"
    "dcc9020068ca020098cf0200000000000001000000000000010000000000000018c9090000"
    "000000480000004001000040020000000000000df0ad0b00000000\n"
    "07000000000000007379735f6d677200940000000100000001000000880000007276356998"
    "360e000800000000000000e6d40d00000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000df0ad0b000000004400000000000000\n"
    "6c850e006c850e00189a0e0010d002006cd8020008da020028de0200000000000001000000"
    "000000010000000000000018c90900010000004800000040010000\n"
    "40020000000000000df0ad0b00000000030000000000000075326600000000009400000001"
    "000000010000008800000072763569628e0e000800000000000000\n"
    "90860e00000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "0df0ad0b000000004400000000000000389a0e00389a0e00b0d00e00a0de0200c8e902001c"
    "eb020080ef02000000000000010000000000000100000000000000\n"
    "18c9090001000000480000004001000040020000000000000df0ad0b000000000900000000"
    "00000070696e776561766572000000000000009400000001000000\n"
    "0100000088000000727635691ca60e0008000000000000009ca50e00000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "00000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000\n"
    "0000000000000000000000000000000000000000000000000df0ad0b00000000ffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    "ffffffffffffffffffffffffffffff";
// Sample GSC (Ti50) crash signature.
constexpr char kGscSignature[] =
    "50710b000b00008000000000000000006b65726e656c0000";
// Sample GSC (Ti50) crash signature offset and size.
const int kCharsPerByte = 2;
const size_t kTi50SignatureStringOffset = 40 * kCharsPerByte;
const size_t kTi50SignatureStringSize = 24 * kCharsPerByte;

class GscCollectorMock : public GscCollectorBase {
 public:
  GscCollectorMock()
      : GscCollectorBase(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
  MOCK_METHOD(GscCollectorBase::Status,
              GetGscFlog,
              (std::string * flog_output),
              (override));
  MOCK_METHOD(GscCollectorBase::Status,
              GetGscClog,
              (std::string * clog_output),
              (override));
  MOCK_METHOD(GscCollectorBase::Status,
              GetGscCrashSignatureOffsetAndLength,
              (size_t * offset_out, size_t* size_out),
              (override));
  MOCK_METHOD(GscCollectorBase::Status,
              PersistGscCrashId,
              (uint32_t crash_id),
              (override));
};

class GscCollectorTest : public ::testing::Test {
 protected:
  void SetUpGscPrevCrashLogId(const std::string gsc_prev_crash_log_id_string);

  const FilePath& test_dir() const { return scoped_temp_dir_.GetPath(); }
  const FilePath& log_messages_file() const { return test_log_messages_; }

  testing::NiceMock<GscCollectorMock> collector_;

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    paths::SetPrefixForTesting(test_dir());
    // TODO(b/276350235): Correctly mock DBus calls.
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(Return());

    collector_.Initialize(false);
    collector_.set_crash_directory_for_test(test_dir());

    test_log_messages_ = test_dir().Append("log_messages");
    ASSERT_FALSE(base::PathExists(test_log_messages_));

    ASSERT_TRUE(base::CreateDirectory(
        paths::Get(paths::kGscPrevCrashLogIdPath).DirName()));
  }

  void TearDown() override { paths::SetPrefixForTesting(base::FilePath()); }

  FilePath test_log_messages_;
  base::ScopedTempDir scoped_temp_dir_;
};

// Create the previous crash log ID file containing the passed in string.
void GscCollectorTest::SetUpGscPrevCrashLogId(
    const std::string gsc_prev_crash_log_id_string) {
  ASSERT_TRUE(test_util::CreateFile(paths::Get(paths::kGscPrevCrashLogIdPath),
                                    gsc_prev_crash_log_id_string));
  ASSERT_TRUE(test_util::CreateFile(log_messages_file(),
                                    "\n[ 0.0000] I can haz boot!"));
}

TEST_F(GscCollectorTest, GetGscFlogFail) {
  // Mock no GSC flog data being returned.
  std::string gsctool_flog_output;

  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Fail)));

  // No crash will be collected, since we couldn't get the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, ParseGscFlogFailNoDate) {
  // Mock a GSC crash with a missing date.
  std::string gsctool_flog_output = ": 0a 00 00 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since we couldn't parse the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Invalid flog line format: ': 0a 00 00 00 00'"));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, ParseGscFlogFailNoCrashNumber) {
  // Mock a GSC crash with a missing crash log ID.
  std::string gsctool_flog_output = "Mar 20 23 15:05:34 : 0a\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since we couldn't parse the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Invalid flog line format: 'Mar 20 23 15:05:34 : 0a'"));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, ParseGscFlogFailInvalidCrashNumberFormat) {
  // Mock a GSC crash with an invalid log ID: only 2 bytes, not 4.
  std::string gsctool_flog_output = "Mar 20 23 15:05:34 : 0a 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since we couldn't parse the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog(
      "Invalid crash number format: '00 00', crash_number_parts.size() = 2"));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, ParseGscFlogFailInvalidCrashNumberParts) {
  // Mock a GSC crash with an invalid log ID: each part is 2 bytes, not 1
  std::string gsctool_flog_output =
      "Mar 20 23 15:05:34 : 0a 0000 0000 0000 0000\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since we couldn't parse the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(
      FindLog("Invalid crash number part: '0000', (*rev_iter).length() = 4"));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, ParseGscFlogFailInvalidCrashNumber) {
  // Mock a GSC crash with an invalid log ID: 'xy' are not hex values.
  std::string gsctool_flog_output = "Mar 20 23 15:05:34 : 0a xy 00 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since we couldn't parse the GSC flog output.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Invalid crash_num_string string: '000000xy'"));
  ASSERT_TRUE(FindLog("Failed to get the GSC flog output."));
}

TEST_F(GscCollectorTest, GscPrevCrashLogIdInvalidNotInteger) {
  // Mock a GSC crash, so we check the previous crash log ID.
  std::string gsctool_flog_output = "Mar 20 23 15:13:11 : 0a 01 00 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Don't mock PersistGscCrashId().
  EXPECT_CALL(collector_, PersistGscCrashId)
      .WillRepeatedly([this](uint32_t crash_id) {
        return collector_.GscCollectorBase::PersistGscCrashId(crash_id);
      });

  SetUpGscPrevCrashLogId("bad string");
  // No crash will be collected, since the `gsc_prev_crash_log_id` file doesn't
  // contain a valid hex value.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Invalid previous GSC crash ID: 'bad string'"));
  ASSERT_TRUE(FindLog("Failed to get the previous GSC crash log ID."));
}

TEST_F(GscCollectorTest, GscPrevCrashLogIdInvalidBadHexString) {
  // Mock a GSC crash, so we check the previous crash log ID.
  std::string gsctool_flog_output = "Mar 20 23 15:13:11 : 0a 01 00 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Don't mock PersistGscCrashId().
  EXPECT_CALL(collector_, PersistGscCrashId)
      .WillRepeatedly([this](uint32_t crash_id) {
        return collector_.GscCollectorBase::PersistGscCrashId(crash_id);
      });

  SetUpGscPrevCrashLogId("10xx");
  // No crash will be collected, since the `gsc_prev_crash_log_id` file doesn't
  // contain a valid integer.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Invalid previous GSC crash ID: '10xx'"));
  ASSERT_TRUE(FindLog("Failed to get the previous GSC crash log ID."));
}

TEST_F(GscCollectorTest, PersistGscCrashIdWriteFileFail) {
  // Mock a GSC crash, so we check the previous crash log ID.
  std::string gsctool_flog_output = "Mar 20 23 15:13:11 : 0a 01 00 00 00\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Mock a PersistGscCrashId(1) failure.
  EXPECT_CALL(collector_, PersistGscCrashId(1))
      .WillOnce(Return(GscCollectorBase::Status::Fail));

  // No crash will be collected, since we couldn't persist the latest crash ID.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Failed to persist latest GSC crash ID."));
  ASSERT_FALSE(base::PathExists(paths::Get(paths::kGscPrevCrashLogIdPath)));
}

TEST_F(GscCollectorTest, NoCrash) {
  // Mock GSC flog output without a crash present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  // No crash will be collected, since there was no crash.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("No GSC crash detected."));
}

TEST_F(GscCollectorTest, MultipleCrashes) {
  // Mock GSC flog output without 2 crashes present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:05:34 : 0a 00 00 00 00\n"
      "Mar 20 23 15:05:34 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:13:11 : 0a 0a 00 00 00\n"
      "Mar 20 23 15:13:11 : 00 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Don't mock PersistGscCrashId().
  EXPECT_CALL(collector_, PersistGscCrashId)
      .WillRepeatedly([this](uint32_t crash_id) {
        return collector_.GscCollectorBase::PersistGscCrashId(crash_id);
      });

  // Crash is collected, since GSC crashes are present without any previous
  // crashes recorded in `gsc_prev_crash_log_id` (0xFFFFFFFF = 4294967295).
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:05:34 : 0a 00 00 00 00"));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:13:11 : 0a 01 00 00 00"));
  ASSERT_TRUE(
      FindLog("Previously reported crash ID: 4294967295, Latest crash ID: 10"));

  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "upload_var_collector=gsc"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "sig=google_security_chip\n"));
}

TEST_F(GscCollectorTest, MultipleCrashesPrevId0) {
  // Mock GSC flog output without 2 crashes present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:05:34 : 0a 00 00 00 00\n"
      "Mar 20 23 15:05:34 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:13:11 : 0a 0a 00 00 00\n"
      "Mar 20 23 15:13:11 : 00 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Don't mock PersistGscCrashId().
  EXPECT_CALL(collector_, PersistGscCrashId)
      .WillRepeatedly([this](uint32_t crash_id) {
        return collector_.GscCollectorBase::PersistGscCrashId(crash_id);
      });

  SetUpGscPrevCrashLogId("0");

  // Crash is collected, since the new crash ID is 10, while the previous is 0.
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:05:34 : 0a 00 00 00 00"));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:13:11 : 0a 0a 00 00 00"));
  ASSERT_TRUE(FindLog("Previously reported crash ID: 0, Latest crash ID: 10"));

  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "upload_var_collector=gsc"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "sig=google_security_chip\n"));
}

TEST_F(GscCollectorTest, MultipleCrashesPrevId12) {
  // Mock GSC flog output without 2 crashes present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:05:34 : 0a 00 00 00 00\n"
      "Mar 20 23 15:05:34 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:13:11 : 0a 0c 00 00 00\n" /* 0xC = 12 */
      "Mar 20 23 15:13:11 : 00 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));

  SetUpGscPrevCrashLogId("12");

  // No crash will be collected, since the new crash ID matches the previous ID.
  ASSERT_FALSE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:05:34 : 0a 00 00 00 00"));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:13:11 : 0a 01 00 00 00"));
  // This also validates the persistent file `gsc_prev_crash_log_id` contains
  // the correct crash ID value.
  ASSERT_TRUE(
      FindLog("Latest crash ID (12) is not more recent than previously "
              "reported crash ID (12)"));
}

TEST_F(GscCollectorTest, GetGscSignatureFail) {
  // Mock GSC flog output without 1 crash present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:05:34 : 0a 00 00 00 00\n"
      "Mar 20 23 15:05:34 : 00 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Mock GSC crash log output to be empty with a Fail status.
  std::string gsctool_clog_output = "Invalid clog";
  EXPECT_CALL(collector_, GetGscClog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_clog_output),
                      Return(GscCollectorBase::Status::Fail)));

  // Crash is collected, since GSC crashes are present without any previous
  // crashes recorded in `gsc_prev_crash_log_id` (0xFFFFFFFF = 4294967295).
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:05:34 : 0a 00 00 00 00"));
  ASSERT_TRUE(
      FindLog("Previously reported crash ID: 4294967295, Latest crash ID: 0"));

  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "upload_var_collector=gsc"));

  // Validate the signature matches |kGscExecName| (google_security_chip), which
  // is used as the default value.
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "sig=google_security_chip\n"));
}

TEST_F(GscCollectorTest, GetGscSignature) {
  // Mock GSC flog output without 1 crash present.
  std::string gsctool_flog_output =
      "Dec 31 69 16:00:00 : 00\n"
      "Dec 31 69 16:02:29 : 00\n"
      "Dec 31 69 16:08:40 : 00\n"
      "Jan 06 70 16:44:38 : 03 03 00 00 00\n"
      "Jan 06 70 17:01:55 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:01:56 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:47 : 00 -- TIMESTAMP UNRELIABLE!\n"
      "Jan 06 70 17:21:48 : 02 -- TIMESTAMP UNRELIABLE!\n"
      "Mar 20 23 15:05:34 : 0a 00 00 00 00\n"
      "Mar 20 23 15:05:34 : 00 -- TIMESTAMP UNRELIABLE!\n";
  EXPECT_CALL(collector_, GetGscFlog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(gsctool_flog_output),
                      Return(GscCollectorBase::Status::Success)));
  // Mock GSC crash log output.
  EXPECT_CALL(collector_, GetGscClog(NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(kGscCrashLog),
                      Return(GscCollectorBase::Status::Success)));
  // Mock GSC signature offset/size.
  EXPECT_CALL(collector_,
              GetGscCrashSignatureOffsetAndLength(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<0>(kTi50SignatureStringOffset),
                      SetArgPointee<1>(kTi50SignatureStringSize),
                      Return(GscCollectorBase::Status::Success)));

  // Crash is collected, since GSC crashes are present without any previous
  // crashes recorded in `gsc_prev_crash_log_id` (0xFFFFFFFF = 4294967295).
  ASSERT_TRUE(collector_.Collect(/*use_saved_lsb=*/true));
  ASSERT_TRUE(FindLog("Found GSC crash: Mar 20 23 15:05:34 : 0a 00 00 00 00"));
  ASSERT_TRUE(
      FindLog("Previously reported crash ID: 4294967295, Latest crash ID: 0"));

  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", "upload_var_collector=gsc"));

  // Validate the signature matches.
  std::string expected_signature =
      base::StringPrintf("sig=%s\n", kGscSignature);
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_dir(), "google_security_chip.*.meta", expected_signature));
}

}  // namespace
