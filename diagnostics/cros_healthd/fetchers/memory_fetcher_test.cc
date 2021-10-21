// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gmock/gmock-actions.h>
#include <gtest/gtest.h>
#include <utility>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/mock_executor_adapter.h"
#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/private/cros_healthd_executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

constexpr char kRelativeProcCpuInfoPath[] = "proc/cpuinfo";
constexpr char kRelativeMeminfoPath[] = "proc/meminfo";
constexpr char kRelativeVmStatPath[] = "proc/vmstat";
constexpr char kRelativeMtkmeDirectoryPath[] = "sys/kernel/mm/mktme";
constexpr char kRelativeMtkmeActivePath[] = "sys/kernel/mm/mktme/active";
constexpr char kRelativeMtkmeActiveAlgorithmPath[] =
    "sys/kernel/mm/mktme/active_algo";
constexpr char kRelativeMtkmeKeyCountPath[] = "sys/kernel/mm/mktme/keycnt";
constexpr char kRelativeMtkmeKeyLengthPath[] = "sys/kernel/mm/mktme/keylen";

constexpr char kFakeMeminfoContents[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedFile[] =
    "Incorrectly formatted meminfo contents.\n";
constexpr char kFakeMeminfoContentsMissingMemtotal[] =
    "MemFree:      873180 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemfree[] =
    "MemTotal:      3906320 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemtotal[] =
    "MemTotal:      3906320kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemfree[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 WrongUnits\nMemAvailable: "
    "     87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "NotAnInteger kB\n";

constexpr char kFakeVmStatContents[] = "foo 98\npgfault 654654\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedFile[] =
    "NoKey\npgfault 71023\n";
constexpr char kFakeVmStatContentsMissingPgfault[] = "foo 9908\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedPgfault[] =
    "pgfault NotAnInteger\n";

constexpr char kFakeMktmeActiveFileContent[] = "1\n";
constexpr char kFakeMktmeActiveAlgorithmFileContent[] = "AES_XTS_256\n";
constexpr char kFakeMktmeKeyCountFileContent[] = "3\n";
constexpr char kFakeMktmeKeyLengthFileContent[] = "256\n";

constexpr mojo_ipc::EncryptionState kExpectedMktmeState =
    mojo_ipc::EncryptionState::kMktmeEnabled;
constexpr mojo_ipc::EncryptionState kExpectedTmeState =
    mojo_ipc::EncryptionState::kTmeEnabled;
constexpr mojo_ipc::CryptoAlgorithm kExpectedActiveAlgorithm =
    mojo_ipc::CryptoAlgorithm::kAesXts256;
constexpr int32_t kExpectedMktmeKeyCount = 3;
constexpr int32_t kExpectedTmeKeyCount = 1;
constexpr int32_t kExpectedEncryptionKeyLength = 256;
constexpr uint32_t kTmeCapabilityMsr = 0x981;
constexpr uint64_t kTmeCapabilityMsrValue = 0x000000f400000004;
constexpr uint32_t kTmeActivateMsr = 0x982;
constexpr uint64_t kTmeActivateMsrValue = 0x000400020000002b;
constexpr char kFakeCpuInfoNoTmeContent[] =
    "cpu family\t: 6\n"
    "model\t: 154\n"
    "flags\t: fpu vme de pse tsc"
    "\0";
constexpr char kFakeCpuInfoTmeContent[] =
    "cpu family\t: 6\n"
    "model\t: 154\n"
    "flags\t: fpu vme tme pse tsc"
    "blah\t: blah"
    "\0";

// Saves |response| to |response_destination|.
void OnGetMemoryResponse(mojo_ipc::MemoryResultPtr* response_update,
                         mojo_ipc::MemoryResultPtr response) {
  *response_update = std::move(response);
}

void VerifyMemoryEncryptionInfo(
    const mojo_ipc::MemoryEncryptionInfoPtr& actual_data,
    mojo_ipc::EncryptionState expected_state,
    mojo_ipc::CryptoAlgorithm expected_algorithm,
    uint32_t expected_key_count,
    uint32_t expected_key_length) {
  ASSERT_FALSE(actual_data.is_null());
  EXPECT_EQ(actual_data->encryption_state, expected_state);
  EXPECT_EQ(actual_data->active_algorithm, expected_algorithm);
  EXPECT_EQ(actual_data->max_key_number, expected_key_count);
  EXPECT_EQ(actual_data->key_length, expected_key_length);
}

class MemoryFetcherTest : public ::testing::Test {
 protected:
  MemoryFetcherTest() = default;
  void SetUp() override {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeProcCpuInfoPath), kFakeCpuInfoNoTmeContent));
  }

  void CreateMkmteEnviroment() {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));
    // Create /sys/kernel/mm/mktme/ directory
    ASSERT_TRUE(
        base::CreateDirectory(root_dir().Append(kRelativeMtkmeDirectoryPath)));
    // Write /sys/kernel/mm/mktme/active file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeMtkmeActivePath),
        kFakeMktmeActiveFileContent));
    // Write /sys/kernel/mm/mktme/active_algo file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeMtkmeActiveAlgorithmPath),
        kFakeMktmeActiveAlgorithmFileContent));
    // Write /sys/kernel/mm/mktme/keycnt file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeMtkmeKeyCountPath),
        kFakeMktmeKeyCountFileContent));
    // Write /sys/kernel/mm/mktme/keylen file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        root_dir().Append(kRelativeMtkmeKeyLengthPath),
        kFakeMktmeKeyLengthFileContent));
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }
  MockExecutorAdapter* mock_executor() { return mock_context_.mock_executor(); }

  mojo_ipc::MemoryResultPtr FetchMemoryInfo() {
    base::RunLoop run_loop;
    mojo_ipc::MemoryResultPtr result;
    memory_fetcher_.FetchMemoryInfo(
        base::BindOnce(&OnGetMemoryResponse, &result));
    run_loop.RunUntilIdle();
    return result;
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  MemoryFetcher memory_fetcher_{&mock_context_};
};

// Test that memory info can be read when it exists.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoWithoutMemoryEncryption) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  EXPECT_EQ(info->total_memory_kib, 3906320);
  EXPECT_EQ(info->free_memory_kib, 873180);
  EXPECT_EQ(info->available_memory_kib, 87980);
  EXPECT_EQ(info->page_faults_since_last_boot, 654654);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcMeminfo) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/meminfo is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemTotal key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemTotal) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemFree key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemFree) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemAvailable key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoNoMemAvailable) {
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemTotal key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemTotal) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemFree key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemFree) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemAvailable key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemAvailable) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcVmStat) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/vmstat is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// contain the pgfault key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatNoPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir().Append(kRelativeVmStatPath),
                                   kFakeVmStatContentsMissingPgfault));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat contains
// an incorrectly formatted pgfault key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcVmStatIncorrectlyFormattedPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedPgfault));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test to handle missing /sys/kernel/mm/mktme directory.
TEST_F(MemoryFetcherTest, MissingMktmeDirectory) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_memory_info());
  const auto& memory_info = result->get_memory_info();
  ASSERT_FALSE(memory_info->memory_encryption_info);
}

// Test to verify mktme info.
TEST_F(MemoryFetcherTest, TestFetchMktmeInfo) {
  CreateMkmteEnviroment();

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_memory_info());
  const auto& memory_info = result->get_memory_info();
  VerifyMemoryEncryptionInfo(memory_info->memory_encryption_info,
                             kExpectedMktmeState, kExpectedActiveAlgorithm,
                             kExpectedMktmeKeyCount,
                             kExpectedEncryptionKeyLength);
}

// Test to handle missing /sys/kernel/mm/mktme/active file.
TEST_F(MemoryFetcherTest, MissingMktmeActiveFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeMtkmeActivePath)));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/active_algo file.
TEST_F(MemoryFetcherTest, MissingMktmeActiveAlgorithmFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(
      base::DeleteFile(root_dir().Append(kRelativeMtkmeActiveAlgorithmPath)));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/key_cnt file.
TEST_F(MemoryFetcherTest, MissingMktmeKeyCountFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeMtkmeKeyCountPath)));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/key_len file.
TEST_F(MemoryFetcherTest, MissingMktmeKeyLengthFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(base::DeleteFile(root_dir().Append(kRelativeMtkmeKeyLengthPath)));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test to verify TME info.
TEST_F(MemoryFetcherTest, TestFetchTmeInfo) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeVmStatPath), kFakeVmStatContents));
  ASSERT_TRUE(DeleteFile(root_dir().Append(kRelativeProcCpuInfoPath)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir().Append(kRelativeProcCpuInfoPath), kFakeCpuInfoTmeContent));

  // Set the mock executor response for ReadMsr calls.
  EXPECT_CALL(*mock_executor(), ReadMsr(_, _))
      .Times(2)
      .WillRepeatedly(
          Invoke([](uint32_t msr_reg,
                    executor_ipc::Executor::ReadMsrCallback callback) {
            executor_ipc::ProcessResult status;
            status.return_code = EXIT_SUCCESS;
            int64_t val = 0;
            if (msr_reg == kTmeCapabilityMsr) {
              val = kTmeCapabilityMsrValue;
            } else if (msr_reg == kTmeActivateMsr) {
              val = kTmeActivateMsrValue;
            } else {
              status.return_code = EXIT_FAILURE;
              status.err = "MSR access not allowed";
            }
            std::move(callback).Run(status.Clone(), val);
          }));

  auto result = FetchMemoryInfo();
  ASSERT_TRUE(result->is_memory_info());
  const auto& memory_info = result->get_memory_info();
  VerifyMemoryEncryptionInfo(memory_info->memory_encryption_info,
                             kExpectedTmeState, kExpectedActiveAlgorithm,
                             kExpectedTmeKeyCount,
                             kExpectedEncryptionKeyLength);
}

}  // namespace
}  // namespace diagnostics
