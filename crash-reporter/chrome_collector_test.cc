// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/chrome_collector.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <base/auto_reset.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <brillo/data_encoding.h>
#include <brillo/syslog_logging.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector.h"
#include "crash-reporter/crash_sending_mode.h"
#include "crash-reporter/test_util.h"

using base::FilePath;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Not;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::TestWithParam;
using ::testing::WithArgs;

namespace {
const char kTestCrashDirectory[] = "test-crash-directory";

// We must have an upload_file_minidump to get a payload name.
const char kCrashFormatGood[] =
    "value1:10:abcdefghijvalue2:5:12345"
    "upload_file_minidump\"; filename=\"dump\":3:abc";
const char kCrashFormatGoodShutdown[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "shutdown-type:5:close";
const char kCrashFormatNoDump[] = "value1:10:abcdefghijvalue2:5:12345";
const char kCrashFormatProcessTypeBrowser[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "ptype:7:browser";
const char kCrashFormatProcessTypeRenderer[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "ptype:8:renderer";
const char kCrashFormatProcessTypeTestString[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "ptype:8:test_str";
const char kCrashFormatProcessTypeBrowserShutdownClose[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "ptype:7:browsershutdown-type:5:close";
const char kCrashFormatProcessTypeBrowserShutdownExit[] =
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "ptype:7:browsershutdown-type:4:exit";
const char kCrashFormatEmbeddedNewline[] =
    "value1:10:abcd\r\nghijvalue2:5:12\n34"
    "upload_file_minidump\"; filename=\"dump\":3:a\nc";
// Inputs that should fail ParseCrashLog regardless of crash_type.
// Format is {input crash log string, expected result}
const std::pair<const char*, CrashCollectionStatus>
    kCrashFormatBadValuesCommon[] = {
        // Last length too long
        {"value1:10:abcdefghijvalue2:6:12345",
         CrashCollectionStatus::kTruncatedChromeDump},
        // Length is followed by something other than a colon.
        {"value1:10:abcdefghijvalue2:5f:12345",
         CrashCollectionStatus::kInvalidSizeNaN},
        // Length not terminated
        {"value1:10:abcdefghijvalue2:5",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedSizeString},
        // No last length.
        {"value1:10:abcdefghijvalue2:",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedNameString},
        // Length value missing
        {"value1:10:abcdefghijvalue2::12345",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedSizeString},
        // Length not a number
        {"value1:10:abcdefghijvalue2:five:12345",
         CrashCollectionStatus::kInvalidSizeNaN},
        // Last length too short
        {"value1:10:abcdefghijvalue2:4:12345",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedNameString},
        // Missing length
        {"value1::abcdefghijvalue2:5:12345",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedSizeString},
        // Missing initial key
        {":5:abcdefghijvalue2:5:12345",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedNameString},
        // Missing later key
        {"value1:10:abcdefghij:5:12345",
         CrashCollectionStatus::kInvalidChromeDumpNoDelimitedNameString},
};
// Inputs that should fail ParseCrashLog if crash_type is kExecutableCrash.
const std::pair<const char*, CrashCollectionStatus>
    kCrashFormatBadValuesExecutable[] = {
        // A JavaScript stack when we expect a minidump
        {"upload_file_js_stack\"; filename=\"stack\":20:0123456789abcdefghij",
         CrashCollectionStatus::kUnexpectedJavaScriptStackInExecutableCrash},
        // Multiple minidumps
        {"upload_file_minidump\"; filename=\"dump\":7:easy as"
         "upload_file_minidump\"; filename=\"dump\":3:123",
         CrashCollectionStatus::kMultipleMinidumps},
};
// Inputs that should fail ParseCrashLog if crash_type is kJavaScriptError.
const std::pair<const char*, CrashCollectionStatus>
    kCrashFormatBadValuesJavaScript[] = {
        // A minidump when we expect a JavaScript stack
        {"upload_file_minidump\"; filename=\"dump\":3:abc",
         CrashCollectionStatus::kUnexpectedMinidumpInJavaScriptError},
        // Multiple js stacks
        {"upload_file_js_stack\"; filename=\"stack\":3:abc"
         "upload_file_js_stack\"; filename=\"stack\":3:123",
         CrashCollectionStatus::kMultipleJavaScriptStacks},
};

const char kCrashFormatWithFile[] =
    "value1:10:abcdefghijvalue2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12345\n789\n12345"
    "upload_file_minidump\"; filename=\"dump\":3:abc"
    "value3:2:ok";

// Matches the :20: in kCrashFormatWithDumpFile
const int kOutputDumpFileSize = 20;
// Matches the :15: in kCrashFormatWithDumpFile
const int kOutputOtherFileSize = 15;

const char kCrashFormatWithDumpFile[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12345\n789\n12345"
    "upload_file_minidump\"; filename=\"dump\":20:0123456789abcdefghij"
    "value3:2:ok";
const char kCrashFormatWithDumpFileWithEmbeddedNulBytes[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12\00045\n789\n12\00045"
    "upload_file_minidump\"; filename=\"dump\":20:"
    "\00012345678\000\a\bcd\x0e\x0fghij"
    "value3:2:ok";
const char kCrashFormatWithWeirdFilename[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "dotdotfile\"; filename=\"../a.txt\":15:12345\n789\n12345"
    "upload_file_minidump\"; filename=\"dump\":20:0123456789abcdefghij"
    "value3:2:ok";
const char kCrashFormatWithJSStack[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12345\n789\n12345"
    "upload_file_js_stack\"; filename=\"stack\":20:0123456789abcdefghij"
    "value3:2:ok";

const char kSampleDriErrorStateBase64Encoded[] =
    "<base64>: SXQgYXBwZWFycyB0byBiZSBzb21lIHNvcnQgb2YgZXJyb3IgZGF0YS4=";
const char kSampleDriErrorStateBase64Decoded[] =
    "It appears to be some sort of error data.";

const char kSampleDriErrorStateBase64EncodedDetailed[] =
    "<base64>: "
    "SXQgYXBwZWFycyB0byBiZSBzb21lIHNvcnQgb2YgZXJyb3IgZGF0YSB3aXRoIGFkZGl0aW9uY"
    "WwgZGV0YWlsLg==";
const char kSampleDriErrorStateBase64DecodedDetailed[] =
    "It appears to be some sort of error data with additional detail.";

const char kSampleDriErrorStateBase64EncodedLong[] =
    "<base64>: "
    "MDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5M"
    "DAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OT"
    "AKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg"
    "5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4"
    "OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2N"
    "zg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Nj"
    "c4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU"
    "2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1"
    "Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzN"
    "DU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMz"
    "Q1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTI"
    "zNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEy"
    "MzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwM"
    "TIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMD"
    "EyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTA"
    "wMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkw"
    "MDEyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4O"
    "TAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3OD"
    "kwMDEyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc"
    "4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3"
    "ODkwMDEyMzQ1Njc4OTAK";

constexpr char kSampleDmesg[] =
    "[   15.945022] binder: 3495:3495 ioctl 4018620d ffdc30c0 returned -22\n"
    "[   17.943062] iio iio:device1: Unable to flush sensor\n";

}  // namespace

class ChromeCollectorMock : public ChromeCollector {
 public:
  ChromeCollectorMock()
      : ChromeCollector(
            CrashSendingMode::kNormal,
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class DebugdProxyMockWithWeakPtr : public org::chromium::debugdProxyMock {
 public:
  ~DebugdProxyMockWithWeakPtr() override = default;

  base::WeakPtrFactory<DebugdProxyMockWithWeakPtr> weak_factory_{this};
};

class ChromeCollectorTest : public ::testing::Test {
 protected:
  void ExpectFileEquals(const char* golden, const FilePath& file_path) {
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(file_path, &contents));
    EXPECT_EQ(golden, contents);
  }

  // Set things up so that the call to get the DriErrorState will return the
  // indicating string. Set to "<empty>" to avoid creating a DriErrorState.
  void SetUpDriErrorStateToReturn(const std::string& log_name,
                                  std::string result) {
    std::function<void(base::OnceCallback<void(const std::string&)>&&)>
        handler = [this, result](
                      base::OnceCallback<void(const std::string&)> callback) {
          task_environment_.GetMainThreadTaskRunner()->PostNonNestableTask(
              FROM_HERE, base::BindOnce(std::move(callback), result));
        };
    CHECK(debugd_proxy_mock_);
    ON_CALL(*debugd_proxy_mock_, GetLogAsync(log_name, _, _, _))
        .WillByDefault(WithArgs<1>(handler));
  }

  void SetUpDriErrorStateToErrorOut(const std::string& log_name,
                                    brillo::Error* error) {
    std::function<void(base::OnceCallback<void(brillo::Error*)>&&)> handler =
        [this, error](base::OnceCallback<void(brillo::Error*)> callback) {
          task_environment_.GetMainThreadTaskRunner()->PostNonNestableTask(
              FROM_HERE, base::BindOnce(std::move(callback), error));
        };
    CHECK(debugd_proxy_mock_);
    ON_CALL(*debugd_proxy_mock_, GetLogAsync(log_name, _, _, _))
        .WillByDefault(WithArgs<2>(handler));
  }

  // Set things up so that the call to CallDmesgAsync will return the
  // indicating string.
  void SetUpCallDmesgToReturn(std::string result) {
    std::function<void(base::OnceCallback<void(const std::string&)>&&)>
        handler = [this, result](
                      base::OnceCallback<void(const std::string&)> callback) {
          task_environment_.GetMainThreadTaskRunner()->PostNonNestableTask(
              FROM_HERE, base::BindOnce(std::move(callback), result));
        };
    CHECK(debugd_proxy_mock_);
    ON_CALL(*debugd_proxy_mock_, CallDmesgAsync(_, _, _, _))
        .WillByDefault(WithArgs<1>(handler));
  }

  // Set things up so that the call to CallDmesgAsync will error out with the
  // indicated Error.
  void SetUpCallDmesgToErrorOut(brillo::Error* error) {
    std::function<void(base::OnceCallback<void(brillo::Error*)>&&)> handler =
        [this, error](base::OnceCallback<void(brillo::Error*)> callback) {
          task_environment_.GetMainThreadTaskRunner()->PostNonNestableTask(
              FROM_HERE, base::BindOnce(std::move(callback), error));
        };
    CHECK(debugd_proxy_mock_);
    ON_CALL(*debugd_proxy_mock_, CallDmesgAsync(_, _, _, _))
        .WillByDefault(WithArgs<2>(handler));
  }

  // Sets up the logs config so that HandleCrash will not produce a
  // chrome.txt.gz file.
  void SetUpLogsNone() {
    base::FilePath config_file =
        scoped_temp_dir_.GetPath().Append("crash_config");
    const char kConfigContents[] = "";
    ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
    collector_.set_log_config_path(config_file.value());
  }

  // Sets up the logs config so that HandleCrash will produce a relatively small
  // chrome.txt.gz.
  void SetUpLogsShort() {
    base::FilePath config_file =
        scoped_temp_dir_.GetPath().Append("crash_config");
    const char kConfigContents[] =
        "chrome=echo hello there\n"
        "jserror=echo JavaScript has nothing to do with Java\n";
    ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
    collector_.set_log_config_path(config_file.value());
  }

  // Sets up the logs config so that HandleCrash will produce a relatively large
  // chrome.txt.gz -- even compressed, should be over 10K.
  void SetUpLogsLong() {
    base::FilePath config_file =
        scoped_temp_dir_.GetPath().Append("crash_config");
    const char kConfigContents[] = "chrome=seq 1 10000";
    ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
    collector_.set_log_config_path(config_file.value());
  }

  void Decompress(const base::FilePath& path) {
    int decompress_result = system(("gunzip " + path.value()).c_str());
    EXPECT_TRUE(WIFEXITED(decompress_result));
    EXPECT_EQ(WEXITSTATUS(decompress_result), 0);
  }

  // Returns a very long string, long enough that even compressed it should be
  // over 10KB.
  std::string GetDmesgLong() {
    std::string result;
    for (int i = 0; i < 20000; i++) {
      base::StrAppend(&result, {kSampleDmesg, base::NumberToString(i), "\n"});
    }
    return result;
  }

  // Expect that the dmesg output file exists and it has compressed contents
  // that, when uncompressed, equal kSampleDmesg. Returns the original
  // (compressed) filename in |output_dmesg_file| and the original compressed
  // size in |dmesg_log_compressed_size|.
  void ExpectSampleDmesg(base::FilePath& output_dmesg_file,
                         int64_t& dmesg_log_compressed_size) {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz",
        &output_dmesg_file));
    EXPECT_TRUE(
        base::GetFileSize(output_dmesg_file, &dmesg_log_compressed_size));
    Decompress(output_dmesg_file);
    base::FilePath output_dmesg_file_uncompressed =
        output_dmesg_file.RemoveFinalExtension();
    std::string dmesg_file_contents;
    EXPECT_TRUE(base::ReadFileToString(output_dmesg_file_uncompressed,
                                       &dmesg_file_contents));
    EXPECT_EQ(dmesg_file_contents, kSampleDmesg);
  }

  // Expect that the dri error state file exists and has contents equal to
  // |expected_contents|. Returns the filename in |output_dri_error_file|.
  void ExpectSampleDriErrorState(const std::string& log_name,
                                 base::FilePath& output_dri_error_file,
                                 const char* expected_contents) {
    const std::string file_name = "chrome_test.*.123." + log_name + ".log.xz";
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        test_crash_directory_, file_name, &output_dri_error_file));
    std::string dri_error_file_contents;
    EXPECT_TRUE(base::ReadFileToString(output_dri_error_file,
                                       &dri_error_file_contents));
    EXPECT_EQ(dri_error_file_contents, expected_contents);
  }

  // Expect that the log output file exists and it has compressed contents
  // that, when uncompressed, equal the message put there by SetUpLogsShort().
  // Returns the original (compressed) filename in |output_log| and the original
  // compressed size in |output_log_compressed_size|.
  void ExpectShortOutputLog(base::FilePath& output_log,
                            int64_t& output_log_compressed_size) {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
        test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", &output_log));
    EXPECT_TRUE(base::GetFileSize(output_log, &output_log_compressed_size));
    Decompress(output_log);
    base::FilePath output_log_uncompressed = output_log.RemoveFinalExtension();
    std::string output_log_contents;
    EXPECT_TRUE(
        base::ReadFileToString(output_log_uncompressed, &output_log_contents));
    EXPECT_EQ(output_log_contents, "hello there\n");
  }

  // RunLoop requires a task environment.
  base::test::SingleThreadTaskEnvironment task_environment_;

  ChromeCollectorMock collector_;
  base::FilePath test_crash_directory_;
  base::ScopedTempDir scoped_temp_dir_;

 private:
  // A properly-lifetimed org::chromium::debugdProxyMock pointer. We keep this
  // one even after passing ownership to the ChromeCollector when
  // SetUpDBus is called.
  base::WeakPtr<DebugdProxyMockWithWeakPtr> debugd_proxy_mock_;
  // The proxy mock we pass to the collector_ when SetUpDBus is called. Private
  // because this is set to nullptr when SetUpDBus is run, so calling
  // EXPECT_CALL(*debugd_proxy_mock_, ...) is dangerous. Better to use
  // debugd_proxy_mock_.
  std::unique_ptr<DebugdProxyMockWithWeakPtr> debugd_proxy_mock_owner_;

  void SetUp() override {
    std::string dummy_to_check_validity;
    ASSERT_TRUE(brillo::data_encoding::Base64Decode(
        kSampleDriErrorStateBase64EncodedLong + strlen("<base64>: "),
        &dummy_to_check_validity));

    collector_.Initialize(false);
    brillo::ClearLog();

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    ASSERT_TRUE(CreateDirectory(test_crash_directory_));
    collector_.set_crash_directory_for_test(test_crash_directory_);
    debugd_proxy_mock_owner_ = std::make_unique<DebugdProxyMockWithWeakPtr>();
    debugd_proxy_mock_ = debugd_proxy_mock_owner_->weak_factory_.GetWeakPtr();
    ON_CALL(collector_, SetUpDBus()).WillByDefault(Invoke([this]() {
      if (debugd_proxy_mock_) {
        collector_.debugd_proxy_ = std::move(debugd_proxy_mock_owner_);
      }
    }));
  }
};

TEST_F(ChromeCollectorTest, GoodValues) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatGood, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_FALSE(collector_.is_shutdown_crash());
  EXPECT_EQ(payload, dir.Append("base.dmp"));
  ExpectFileEquals("abc", payload);

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
}

TEST_F(ChromeCollectorTest, GoodShutdown) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatGoodShutdown, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(collector_.is_shutdown_crash());
  EXPECT_EQ(payload, dir.Append("base.dmp"));
  ExpectFileEquals("abc", payload);

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("upload_var_shutdown-type=close") != std::string::npos);
}

TEST_F(ChromeCollectorTest, ProcessTypeCheck) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;

  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatProcessTypeBrowser, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_FALSE(collector_.is_shutdown_crash());

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("ptype=browser") != std::string::npos);
}

TEST_F(ChromeCollectorTest, HandleCrashWithDumpData_JavaScriptError) {
  // Success because JavaScript errors are allowed to have no stack.
  EXPECT_EQ(collector_.HandleCrashWithDumpData("", 1, 1, "", "test_key", "", "",
                                               "", 1),
            CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(collector_.IsJavaScriptError());
}

TEST_F(ChromeCollectorTest, HandleCrashWithDumpData_ExecutableCrash) {
  EXPECT_EQ(collector_.HandleCrashWithDumpData("", 1, 1, "sample_executable",
                                               "", "", "", "", 1),
            CrashCollectionStatus::kNoPayload);
  EXPECT_FALSE(collector_.IsJavaScriptError());
}

TEST_F(ChromeCollectorTest, ParseCrashLogNoDump) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatNoDump, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_EQ(payload.value(), "");
  EXPECT_FALSE(base::PathExists(dir.Append("base.dmp")));

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
}

TEST_F(ChromeCollectorTest, ParseCrashLogJSStack) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatWithJSStack, dir, "base",
                               ChromeCollector::kJavaScriptError, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_EQ(payload, dir.Append("base.js_stack"));
  ExpectFileEquals("0123456789abcdefghij", payload);

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
}

TEST_F(ChromeCollectorTest, Newlines) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatEmbeddedNewline, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_EQ(payload, dir.Append("base.dmp"));
  ExpectFileEquals("a\nc", payload);

  // Check to see if the values were escaped.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcd\\r\\nghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12\\n34") != std::string::npos);
}

TEST_F(ChromeCollectorTest, BadValues) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  int test_number = 0;
  for (const auto& data_result_pair : kCrashFormatBadValuesCommon) {
    const char* data = data_result_pair.first;
    CrashCollectionStatus expected_result = data_result_pair.second;
    for (auto crash_type : {ChromeCollector::kExecutableCrash,
                            ChromeCollector::kJavaScriptError}) {
      FilePath payload;
      EXPECT_EQ(collector_.ParseCrashLog(
                    data, dir,
                    base::StrCat(
                        {"base_", base::NumberToString(test_number), "_test"}),
                    crash_type, &payload),
                expected_result)
          << data << " did not fail (for crash_type "
          << static_cast<int>(crash_type) << ")";
      test_number++;
    }
  }
  for (const auto& data_result_pair : kCrashFormatBadValuesExecutable) {
    const char* data = data_result_pair.first;
    CrashCollectionStatus expected_result = data_result_pair.second;
    FilePath payload;
    EXPECT_EQ(
        collector_.ParseCrashLog(
            data, dir,
            base::StrCat({"base_", base::NumberToString(test_number), "_test"}),
            ChromeCollector::kExecutableCrash, &payload),
        expected_result)
        << data << " did not fail";
    test_number++;
  }
  for (const auto& data_result_pair : kCrashFormatBadValuesJavaScript) {
    const char* data = data_result_pair.first;
    CrashCollectionStatus expected_result = data_result_pair.second;
    FilePath payload;
    EXPECT_EQ(
        collector_.ParseCrashLog(
            data, dir,
            base::StrCat({"base_", base::NumberToString(test_number), "_test"}),
            ChromeCollector::kJavaScriptError, &payload),
        expected_result)
        << data << " did not fail";
    test_number++;
  }
}

TEST_F(ChromeCollectorTest, File) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  FilePath payload;
  EXPECT_EQ(
      collector_.ParseCrashLog(kCrashFormatWithFile, dir, "base",
                               ChromeCollector::kExecutableCrash, &payload),
      CrashCollectionStatus::kSuccess);
  EXPECT_EQ(payload, dir.Append("base.dmp"));
  ExpectFileEquals("abc", payload);

  // Check to see if the values are still correct and that the file was
  // written with the right data.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
  EXPECT_TRUE(meta.find("value3=ok") != std::string::npos);
  ExpectFileEquals("12345\n789\n12345", dir.Append("base-foo_txt.other"));
}

TEST_F(ChromeCollectorTest, HandleCrash) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  FilePath log_file;
  {
    base::ScopedFILE output(
        base::CreateAndOpenTemporaryStreamInDir(dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_EQ(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
        CrashCollectionStatus::kSuccess);
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithEmbeddedNuls) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  std::string input(kCrashFormatWithDumpFileWithEmbeddedNulBytes,
                    sizeof(kCrashFormatWithDumpFileWithEmbeddedNulBytes) - 1);
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, input));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  FilePath log_file;
  {
    base::ScopedFILE output(
        base::CreateAndOpenTemporaryStreamInDir(dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_EQ(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
        CrashCollectionStatus::kSuccess);
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  std::string expected_dump_contents("\00012345678\000\a\bcd\x0e\x0fghij", 20);
  EXPECT_EQ(output_dump_file_contents, expected_dump_contents);

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  std::string expected_other_contents("12\00045\n789\n12\00045", 15);
  EXPECT_EQ(other_file_contents, expected_other_contents);

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithWeirdFilename) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  std::string input(kCrashFormatWithWeirdFilename,
                    sizeof(kCrashFormatWithWeirdFilename) - 1);
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, input));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  FilePath log_file;
  {
    base::ScopedFILE output(
        base::CreateAndOpenTemporaryStreamInDir(dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_EQ(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
        CrashCollectionStatus::kSuccess);
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-___a_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_dotdotfile=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithLogsAndDriErrorStateAndDmesg) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedDetailed);
  SetUpCallDmesgToReturn(kSampleDmesg);
  SetUpLogsShort();

  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  base::FilePath output_encoded_dri_error_file, output_decoded_dri_error_file;
  ExpectSampleDriErrorState("i915_error_state", output_encoded_dri_error_file,
                            kSampleDriErrorStateBase64Decoded);
  ExpectSampleDriErrorState("i915_error_state_decoded",
                            output_decoded_dri_error_file,
                            kSampleDriErrorStateBase64DecodedDetailed);

  base::FilePath output_dmesg_file;
  int64_t dmesg_log_compressed_size = 0;
  ExpectSampleDmesg(output_dmesg_file, dmesg_log_compressed_size);

  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_log_compressed_size +
                dmesg_log_compressed_size +
                strlen(kSampleDriErrorStateBase64Decoded) +
                strlen(kSampleDriErrorStateBase64DecodedDetailed) +
                other_file_contents.size() + output_dump_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz=" +
                        output_encoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state_decoded.log.xz=" +
                        output_decoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_dmesg.txt=" +
                        output_dmesg_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsSupplementalFilesIfDumpFileLarge) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedDetailed);
  SetUpCallDmesgToReturn(kSampleDmesg);
  SetUpLogsShort();
  // Make dmp file "too large"
  collector_.set_max_upload_bytes_for_test(1);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  // Supplemental files not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(
      collector_.get_bytes_written(),
      meta_file_contents.size() + kOutputDumpFileSize + kOutputOtherFileSize);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state_decoded.log.xz")));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeLogFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedDetailed);
  SetUpCallDmesgToReturn(kSampleDmesg);
  SetUpLogsLong();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  // Log file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));

  // Error state & dmesg file still written even after log file rejected.
  base::FilePath output_encoded_dri_error_file, output_decoded_dri_error_file;
  ExpectSampleDriErrorState("i915_error_state", output_encoded_dri_error_file,
                            kSampleDriErrorStateBase64Decoded);
  ExpectSampleDriErrorState("i915_error_state_decoded",
                            output_decoded_dri_error_file,
                            kSampleDriErrorStateBase64DecodedDetailed);

  base::FilePath output_dmesg_file;
  int64_t dmesg_log_compressed_size = 0;
  ExpectSampleDmesg(output_dmesg_file, dmesg_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                dmesg_log_compressed_size + kOutputOtherFileSize +
                strlen(kSampleDriErrorStateBase64Decoded) +
                strlen(kSampleDriErrorStateBase64DecodedDetailed));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt")));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz=" +
                        output_encoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state_decoded.log.xz=" +
                        output_decoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_dmesg.txt=" +
                        output_dmesg_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeDriErrorFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedLong);
  SetUpCallDmesgToReturn(kSampleDmesg);
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  // Large Dri Error State file not written, but small one written.
  base::FilePath output_dri_error_file;
  ExpectSampleDriErrorState("i915_error_state", output_dri_error_file,
                            kSampleDriErrorStateBase64Decoded);

  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));

  // Log & dmesg files still written even after Dri Error State file rejected.
  base::FilePath output_dmesg_file;
  int64_t dmesg_log_compressed_size = 0;
  ExpectSampleDmesg(output_dmesg_file, dmesg_log_compressed_size);

  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                strlen(kSampleDriErrorStateBase64Decoded) +
                dmesg_log_compressed_size + kOutputOtherFileSize +
                output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz"));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state_decoded.log.xz")));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_dmesg.txt=" +
                        output_dmesg_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeDmesgFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedDetailed);
  SetUpCallDmesgToReturn(GetDmesgLong());
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  // dmesg file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // Log & dri error files still written even after dmesg file rejected.
  base::FilePath output_encoded_dri_error_file, output_decoded_dri_error_file;
  ExpectSampleDriErrorState("i915_error_state", output_encoded_dri_error_file,
                            kSampleDriErrorStateBase64Decoded);
  ExpectSampleDriErrorState("i915_error_state_decoded",
                            output_decoded_dri_error_file,
                            kSampleDriErrorStateBase64DecodedDetailed);

  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                strlen(kSampleDriErrorStateBase64Decoded) +
                strlen(kSampleDriErrorStateBase64DecodedDetailed) +
                kOutputOtherFileSize + output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz=" +
                        output_encoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state_decoded.log.xz=" +
                        output_decoded_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeSupplementalFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64EncodedLong);
  SetUpDriErrorStateToReturn("i915_error_state_decoded",
                             kSampleDriErrorStateBase64EncodedLong);
  SetUpCallDmesgToReturn(GetDmesgLong());
  SetUpLogsLong();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  // Dri Error State file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));

  // dmesg file not written
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // Log file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(
      collector_.get_bytes_written(),
      meta_file_contents.size() + kOutputDumpFileSize + kOutputOtherFileSize);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrash_GetCreatedCrashDirectoryByEuidFailure) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();
  collector_.force_get_created_crash_directory_by_euid_status_for_test(
      CrashCollectionStatus::kOutOfCapacity, true);

  FilePath log_file;
  {
    base::ScopedFILE output(
        base::CreateAndOpenTemporaryStreamInDir(dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_EQ(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
        CrashCollectionStatus::kOutOfCapacity);
  }
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(test_crash_directory_,
                                                      "*.123.dmp", nullptr));

  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "*.123-foo_txt.other", nullptr));

  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(test_crash_directory_,
                                                      "*.123.meta", nullptr));
}

TEST_F(ChromeCollectorTest, HandleCrashWithDumpData_ShutdownHang) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath aborted_browser_pid_file = dir.Append("aborted_browser_pid");
  FilePath shutdown_browser_pid_file = dir.Append("shutdown_browser_pid");
  ASSERT_TRUE(test_util::CreateFile(shutdown_browser_pid_file, "123"));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  EXPECT_EQ(collector_.HandleCrashWithDumpData(
                kCrashFormatWithDumpFile, 123, 456, "chrome_test", "", "",
                aborted_browser_pid_file.value(),
                shutdown_browser_pid_file.value(), -1),
            CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(collector_.is_browser_shutdown_hang());
  EXPECT_FALSE(collector_.is_signal_fatal());
}

TEST_F(ChromeCollectorTest,
       HandleCrashWithDumpData_NotShutdownHang_NoShutdownBrowserPidFile) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath aborted_browser_pid_file = dir.Append("aborted_browser_pid");
  FilePath shutdown_browser_pid_file = dir.Append("shutdown_browser_pid");
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  EXPECT_EQ(collector_.HandleCrashWithDumpData(
                kCrashFormatWithDumpFile, 123, 456, "chrome_test", "", "",
                aborted_browser_pid_file.value(),
                shutdown_browser_pid_file.value(), -1),
            CrashCollectionStatus::kSuccess);
  EXPECT_FALSE(collector_.is_browser_shutdown_hang());
  EXPECT_FALSE(collector_.is_signal_fatal());
}

TEST_F(ChromeCollectorTest,
       HandleCrashWithDumpData_NotShutdownHang_WrongShutdownBrowserPid) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath aborted_browser_pid_file = dir.Append("aborted_browser_pid");
  FilePath shutdown_browser_pid_file = dir.Append("shutdown_browser_pid");
  ASSERT_TRUE(test_util::CreateFile(shutdown_browser_pid_file, "77"));
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  EXPECT_EQ(collector_.HandleCrashWithDumpData(
                kCrashFormatWithDumpFile, 123, 456, "chrome_test", "", "",
                aborted_browser_pid_file.value(),
                shutdown_browser_pid_file.value(), -1),
            CrashCollectionStatus::kSuccess);
  EXPECT_FALSE(collector_.is_browser_shutdown_hang());
  EXPECT_FALSE(collector_.is_signal_fatal());
}

TEST_F(ChromeCollectorTest, HandleCrashWithDumpData_Signal_Fatal) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath aborted_browser_pid_file = dir.Append("aborted_browser_pid");
  FilePath shutdown_browser_pid_file = dir.Append("shutdown_browser_pid");
  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  EXPECT_EQ(collector_.HandleCrashWithDumpData(
                kCrashFormatWithDumpFile, 123, 456, "chrome_test", "", "",
                aborted_browser_pid_file.value(),
                shutdown_browser_pid_file.value(), 11),
            CrashCollectionStatus::kSuccess);
  EXPECT_TRUE(collector_.is_signal_fatal());
}

TEST_F(ChromeCollectorTest, HandleDbusTimeouts) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToErrorOut("i915_error_state", nullptr);
  SetUpDriErrorStateToErrorOut("i915_error_state_decoded", nullptr);
  SetUpCallDmesgToErrorOut(nullptr);
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  EXPECT_TRUE(brillo::FindLog(
      "Error retrieving DriErrorState from debugd: Call did not return"));
  EXPECT_TRUE(brillo::FindLog(
      "Error retrieving dmesg from debugd: Call did not return"));

  // Dri Error State file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));

  // dmesg file not written
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // Log file still written
  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                kOutputOtherFileSize + output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state_decoded.log.xz")));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleDbusErrors) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  brillo::ErrorPtr dir_error_state_error = brillo::Error::CreateNoLog(
      FROM_HERE, /*domain=*/"source.chromium.org", /*code=*/"EBAD",
      /*message=*/"dri_error_state retrieval failed", /*inner_error=*/nullptr);
  brillo::ErrorPtr dmesg_error = brillo::Error::CreateNoLog(
      FROM_HERE, /*domain=*/"source.chromium.org", /*code=*/"EPERM",
      /*message=*/"dmesg no permission", /*inner_error=*/nullptr);
  SetUpDriErrorStateToErrorOut("i915_error_state", dir_error_state_error.get());
  SetUpDriErrorStateToErrorOut("i915_error_state_decoded",
                               dir_error_state_error.get());
  SetUpCallDmesgToErrorOut(dmesg_error.get());
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  EXPECT_TRUE(
      brillo::FindLog("Error retrieving DriErrorState from debugd: "
                      "dri_error_state retrieval failed"));
  EXPECT_TRUE(brillo::FindLog(
      "Error retrieving dmesg from debugd: dmesg no permission"));

  // Dri Error State file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));

  // dmesg file not written
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // Log file still written
  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                kOutputOtherFileSize + output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state_decoded.log.xz")));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandlePartialDbusErrors) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  brillo::ErrorPtr dir_error_state_error = brillo::Error::CreateNoLog(
      FROM_HERE, /*domain=*/"source.chromium.org", /*code=*/"EBAD",
      /*message=*/"dri_error_state retrieval failed", /*inner_error=*/nullptr);
  brillo::ErrorPtr dmesg_error = brillo::Error::CreateNoLog(
      FROM_HERE, /*domain=*/"source.chromium.org", /*code=*/"EPERM",
      /*message=*/"dmesg no permission", /*inner_error=*/nullptr);
  SetUpDriErrorStateToReturn("i915_error_state",
                             kSampleDriErrorStateBase64Encoded);
  SetUpDriErrorStateToErrorOut("i915_error_state_decoded",
                               dir_error_state_error.get());
  SetUpCallDmesgToErrorOut(dmesg_error.get());
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_EQ(
      collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test", -1),
      CrashCollectionStatus::kSuccess);

  EXPECT_TRUE(
      brillo::FindLog("Error retrieving DriErrorState from debugd: "
                      "dri_error_state retrieval failed"));
  EXPECT_TRUE(brillo::FindLog(
      "Error retrieving dmesg from debugd: dmesg no permission"));

  // Dri Error State failing file not written, but successful file written.
  base::FilePath output_dri_error_file;
  ExpectSampleDriErrorState("i915_error_state", output_dri_error_file,
                            kSampleDriErrorStateBase64Decoded);

  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_,
      "chrome_test.*.123.i915_error_state_decoded.log.xz", nullptr));

  // dmesg file not written
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmesg.txt.gz", nullptr));

  // Log file still written
  base::FilePath output_log;
  int64_t output_log_compressed_size = 0;
  ExpectShortOutputLog(output_log, output_log_compressed_size);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                strlen(kSampleDriErrorStateBase64Decoded) +
                kOutputOtherFileSize + output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz"));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state_decoded.log.xz")));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_dmesg.txt")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

TEST_F(ChromeCollectorTest, HandleCrashForJavaScript) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_file = dir.Append("test.jsinput");
  ASSERT_TRUE(test_util::CreateFile(input_file, kCrashFormatWithJSStack));
  SetUpLogsShort();

  int input_fd = open(input_file.value().c_str(), O_RDONLY);
  ASSERT_NE(input_fd, -1) << "open " << input_file.value() << " failed: "
                          << logging::SystemErrorCodeToString(errno);
  // HandleCrashThroughMemfd will close input_fd.
  EXPECT_EQ(collector_.HandleCrashThroughMemfd(input_fd, 123, 456, "",
                                               "jserror", "", -1),
            CrashCollectionStatus::kSuccess);

  base::FilePath output_dri_error_file;
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "jserror.*.123.i915_error_state.log.xz",
      &output_dri_error_file));

  base::FilePath output_log;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "jserror.*.123.chrome.txt.gz", &output_log));
  int64_t output_log_compressed_size = 0;
  EXPECT_TRUE(base::GetFileSize(output_log, &output_log_compressed_size));
  Decompress(output_log);
  base::FilePath output_log_uncompressed = output_log.RemoveFinalExtension();
  std::string output_log_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_log_uncompressed, &output_log_contents));
  EXPECT_EQ(output_log_contents, "JavaScript has nothing to do with Java\n");

  base::FilePath output_stack_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "jserror.*.123.js_stack", &output_stack_file));
  std::string output_stack_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_stack_file, &output_stack_file_contents));
  EXPECT_EQ(output_stack_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "jserror.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "jserror.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_log_compressed_size +
                other_file_contents.size() + output_stack_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_stack_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
  EXPECT_THAT(meta_file_contents, HasSubstr("done=1"));
  EXPECT_THAT(meta_file_contents, HasSubstr("crashpad_signal_number=-1"));
}

struct ChromeCollectorComputeCrashSeverityTestCase {
  std::string test_name;
  std::string data;
  pid_t pid;
  uid_t uid;
  std::string exec_name;
  std::string non_exe_error_key;
  bool uses_shutdown_browser_pid_path;
  int signal;
  CrashCollector::CrashSeverity expected_severity;
  CrashCollector::Product expected_product;
};

class ComputeChromeCollectorCrashSeverityParameterizedTest
    : public ::ChromeCollectorTest,
      public ::testing::WithParamInterface<
          ChromeCollectorComputeCrashSeverityTestCase> {};

TEST_P(ComputeChromeCollectorCrashSeverityParameterizedTest,
       ComputeCrashSeverity_ChromeCollector) {
  const ChromeCollectorComputeCrashSeverityTestCase& test_case = GetParam();

  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath shutdown_browser_pid_file = dir.Append("shutdown_browser_pid");

  if (test_case.uses_shutdown_browser_pid_path) {
    ASSERT_TRUE(test_util::CreateFile(shutdown_browser_pid_file, "123"));
  }

  SetUpDriErrorStateToReturn("i915_error_state", "<empty>");
  SetUpDriErrorStateToReturn("i915_error_state_decoded", "<empty>");
  SetUpCallDmesgToReturn("");
  SetUpLogsNone();

  EXPECT_EQ(collector_.HandleCrashWithDumpData(
                test_case.data, test_case.pid, test_case.uid,
                test_case.exec_name, test_case.non_exe_error_key,
                /* dump_dir= */ "", /* aborted_browser_pid_path= */ "",
                (test_case.uses_shutdown_browser_pid_path)
                    ? shutdown_browser_pid_file.value()
                    : "",
                test_case.signal),
            CrashCollectionStatus::kSuccess);

  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("test_exec_name");

  EXPECT_EQ(computed_severity.crash_severity, test_case.expected_severity);
  EXPECT_EQ(computed_severity.product_group, test_case.expected_product);
}

INSTANTIATE_TEST_SUITE_P(
    ComputeChromeCollectorCrashSeverityParameterizedTest,
    ComputeChromeCollectorCrashSeverityParameterizedTest,
    testing::ValuesIn<ChromeCollectorComputeCrashSeverityTestCase>({
        {/* test_name= */ "JavascriptError_UI", /* data= */ "",
         /* pid= */ 1, /* uid= */ 1, /* exec_name= */ "",
         /* non_exe_error_key= */ "test_key",
         /* uses_shutdown_browser_pid_path= */ false, /* signal= */ 1,
         CrashCollector::CrashSeverity::kWarning, CrashCollector::Product::kUi},
        {/* test_name= */ "NonFatalSignal_UI",
         /* data= */ kCrashFormatProcessTypeBrowser,
         /* pid= */ 1, /* uid= */ 1, /* exec_name= */ "test_exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ false, /* signal= */ -1,
         CrashCollector::CrashSeverity::kInfo, CrashCollector::Product::kUi},
        {/* test_name= */ "ProcessTypeRenderer_UI",
         /* data= */ kCrashFormatProcessTypeRenderer, /* pid= */ 77,
         /* uid= */ 1, /* exec_name= */ "exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ true, /* signal= */ 1,
         CrashCollector::CrashSeverity::kError, CrashCollector::Product::kUi},
        {/* test_name= */ "ProcessTypeTestString_UI",
         /* data= */ kCrashFormatProcessTypeTestString, /* pid= */ 77,
         /* uid= */ 1, /* exec_name= */ "exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ true, /* signal= */ 1,
         CrashCollector::CrashSeverity::kInfo, CrashCollector::Product::kUi},
        {/* test_name= */ "ProcessTypeBrowser_UI",
         /* data= */ kCrashFormatProcessTypeBrowser, /* pid= */ 77,
         /* uid= */ 1, /* exec_name= */ "exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ true, /* signal= */ 1,
         CrashCollector::CrashSeverity::kFatal, CrashCollector::Product::kUi},
        {/* test_name= */ "BrowserShutdownHang_UI",
         /* data= */ kCrashFormatProcessTypeBrowserShutdownExit,
         /* pid= */ 123,
         /* uid= */ 1, /* exec_name= */ "exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ true, /* signal= */ 1,
         CrashCollector::CrashSeverity::kWarning, CrashCollector::Product::kUi},
        {/* test_name= */ "ShutdownCrash_UI",
         /* data= */ kCrashFormatProcessTypeBrowserShutdownClose,
         /* pid= */ 77,
         /* uid= */ 1, /* exec_name= */ "exec_name",
         /* non_exe_error_key= */ "",
         /* uses_shutdown_browser_pid_path= */ true, /* signal= */ 1,
         CrashCollector::CrashSeverity::kError, CrashCollector::Product::kUi},
    }),
    [](const testing::TestParamInfo<
        ComputeChromeCollectorCrashSeverityParameterizedTest::ParamType>&
           info) { return info.param.test_name; });
