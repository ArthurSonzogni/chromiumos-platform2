// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"
#include "diagnostics/mojom/public/nullable_primitives.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;

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

constexpr mojom::EncryptionState kExpectedMktmeState =
    mojom::EncryptionState::kMktmeEnabled;
constexpr mojom::EncryptionState kExpectedTmeState =
    mojom::EncryptionState::kTmeEnabled;
constexpr mojom::CryptoAlgorithm kExpectedActiveAlgorithm =
    mojom::CryptoAlgorithm::kAesXts256;
constexpr int32_t kExpectedMktmeKeyCount = 3;
constexpr int32_t kExpectedTmeKeyCount = 1;
constexpr int32_t kExpectedEncryptionKeyLength = 256;
constexpr uint64_t kTmeCapabilityMsrValue = 0x000000f400000004;
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

void VerifyMemoryEncryptionInfo(
    const mojom::MemoryEncryptionInfoPtr& actual_data,
    mojom::EncryptionState expected_state,
    mojom::CryptoAlgorithm expected_algorithm,
    uint32_t expected_key_count,
    uint32_t expected_key_length) {
  ASSERT_FALSE(actual_data.is_null());
  EXPECT_EQ(actual_data->encryption_state, expected_state);
  EXPECT_EQ(actual_data->active_algorithm, expected_algorithm);
  EXPECT_EQ(actual_data->max_key_number, expected_key_count);
  EXPECT_EQ(actual_data->key_length, expected_key_length);
}

class MemoryFetcherTest : public BaseFileTest {
 protected:
  MemoryFetcherTest() = default;
  MemoryFetcherTest(const MemoryFetcherTest&) = delete;
  MemoryFetcherTest& operator=(const MemoryFetcherTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeProcCpuInfoPath),
        kFakeCpuInfoNoTmeContent));
  }

  void CreateMkmteEnviroment() {
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));
    // Create /sys/kernel/mm/mktme/ directory
    ASSERT_TRUE(base::CreateDirectory(
        GetRootDir().Append(kRelativeMtkmeDirectoryPath)));
    // Write /sys/kernel/mm/mktme/active file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeMtkmeActivePath),
        kFakeMktmeActiveFileContent));
    // Write /sys/kernel/mm/mktme/active_algo file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeMtkmeActiveAlgorithmPath),
        kFakeMktmeActiveAlgorithmFileContent));
    // Write /sys/kernel/mm/mktme/keycnt file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeMtkmeKeyCountPath),
        kFakeMktmeKeyCountFileContent));
    // Write /sys/kernel/mm/mktme/keylen file.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeMtkmeKeyLengthPath),
        kFakeMktmeKeyLengthFileContent));
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  mojom::MemoryResultPtr FetchMemoryInfoSync() {
    base::test::TestFuture<mojom::MemoryResultPtr> future;
    FetchMemoryInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
};

// Test that memory info can be read when it exists.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoWithoutMemoryEncryption) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  EXPECT_EQ(info->total_memory_kib, 3906320);
  EXPECT_EQ(info->free_memory_kib, 873180);
  EXPECT_EQ(info->available_memory_kib, 87980);
  EXPECT_EQ(info->page_faults_since_last_boot, 654654);
}

// Test that fetching memory info returns an error when /proc/meminfo is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
  EXPECT_EQ(result->get_error()->msg, "Error parsing /proc/meminfo");
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcVmStat) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/vmstat is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatFormattedIncorrectly) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedFile));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// contain the pgfault key.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatNoPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(GetRootDir().Append(kRelativeVmStatPath),
                                   kFakeVmStatContentsMissingPgfault));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat contains
// an incorrectly formatted pgfault key.
TEST_F(MemoryFetcherTest,
       TestFetchMemoryInfoProcVmStatIncorrectlyFormattedPgfault) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedPgfault));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test to handle missing /sys/kernel/mm/mktme directory.
TEST_F(MemoryFetcherTest, MissingMktmeDirectory) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& memory_info = result->get_memory_info();
  ASSERT_FALSE(memory_info->memory_encryption_info);
}

// Test to verify mktme info.
TEST_F(MemoryFetcherTest, TestFetchMktmeInfo) {
  CreateMkmteEnviroment();

  auto result = FetchMemoryInfoSync();
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
  ASSERT_TRUE(
      brillo::DeleteFile(GetRootDir().Append(kRelativeMtkmeActivePath)));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/active_algo file.
TEST_F(MemoryFetcherTest, MissingMktmeActiveAlgorithmFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(brillo::DeleteFile(
      GetRootDir().Append(kRelativeMtkmeActiveAlgorithmPath)));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/key_cnt file.
TEST_F(MemoryFetcherTest, MissingMktmeKeyCountFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(
      brillo::DeleteFile(GetRootDir().Append(kRelativeMtkmeKeyCountPath)));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test to handle missing /sys/kernel/mm/mktme/key_len file.
TEST_F(MemoryFetcherTest, MissingMktmeKeyLengthFile) {
  CreateMkmteEnviroment();
  ASSERT_TRUE(
      brillo::DeleteFile(GetRootDir().Append(kRelativeMtkmeKeyLengthPath)));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test to verify TME info.
TEST_F(MemoryFetcherTest, TestFetchTmeInfo) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));
  ASSERT_TRUE(DeleteFile(GetRootDir().Append(kRelativeProcCpuInfoPath)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeProcCpuInfoPath), kFakeCpuInfoTmeContent));

  // Set the mock executor response for ReadMsr calls.
  EXPECT_CALL(*mock_executor(), ReadMsr(_, _, _))
      .Times(2)
      .WillRepeatedly([](uint32_t msr_reg, uint32_t cpu_index,
                         mojom::Executor::ReadMsrCallback callback) {
        switch (msr_reg) {
          case cpu_msr::kIA32TmeCapability:
            std::move(callback).Run(
                mojom::NullableUint64::New(kTmeCapabilityMsrValue));
            return;
          case cpu_msr::kIA32TmeActivate:
            std::move(callback).Run(
                mojom::NullableUint64::New(kTmeActivateMsrValue));
            return;
          default:
            std::move(callback).Run(nullptr);
        }
      });

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& memory_info = result->get_memory_info();
  VerifyMemoryEncryptionInfo(memory_info->memory_encryption_info,
                             kExpectedTmeState, kExpectedActiveAlgorithm,
                             kExpectedTmeKeyCount,
                             kExpectedEncryptionKeyLength);
}

}  // namespace
}  // namespace diagnostics
