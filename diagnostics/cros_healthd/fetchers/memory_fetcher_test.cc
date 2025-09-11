// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/memory_fetcher.h"

#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/errors/error.h>
#include <brillo/files/file_util.h>
#include <concierge/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <session_manager/dbus-proxy-mocks.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/executor/constants.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/fake_meminfo_reader.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;

constexpr char kRelativeProcCpuInfoPath[] = "proc/cpuinfo";
constexpr char kRelativeVmStatPath[] = "proc/vmstat";
constexpr char kRelativeMtkmeDirectoryPath[] = "sys/kernel/mm/mktme";
constexpr char kRelativeMtkmeActivePath[] = "sys/kernel/mm/mktme/active";
constexpr char kRelativeMtkmeActiveAlgorithmPath[] =
    "sys/kernel/mm/mktme/active_algo";
constexpr char kRelativeMtkmeKeyCountPath[] = "sys/kernel/mm/mktme/keycnt";
constexpr char kRelativeMtkmeKeyLengthPath[] = "sys/kernel/mm/mktme/keylen";

constexpr char kFakeIomemContents[] =
    "00000000-1f81c8ffe : System RAM\n";  // 8259363 kib
constexpr char kFakeIomemContentsIncorrectlyFormattedFile[] =
    "Incorrectly formatted iomem contents.\n";

constexpr char kFakeSmapsContents[] = R"(
7980712e6000-79813e7e6000 rw-s 00100000 00:01 164  /memfd:crosvm_guest (deleted)
Size:            3363840 kB
Rss:             2243936 kB
Swap:             490460 kB
)";

constexpr int kFakeCrosvmProcessId = 2;

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
 public:
  MemoryFetcherTest(const MemoryFetcherTest&) = delete;
  MemoryFetcherTest& operator=(const MemoryFetcherTest&) = delete;

 protected:
  MemoryFetcherTest() = default;

  void SetUp() override {
    ON_CALL(*mock_executor(), ReadFile(mojom::Executor::File::kProcIomem, _))
        .WillByDefault(base::test::RunOnceCallback<1>(kFakeIomemContents));

    // Set up the fake session manager behaviors.
    ON_CALL(*mock_context_.mock_session_manager_proxy(),
            RetrievePrimarySessionAsync(_, _, _))
        .WillByDefault(base::test::RunOnceCallback<0>("testaccount@example.com",
                                                      "0123456789abcdef"));

    // Set up the fake crosvm cmdline file.
    crosvm_proc_ = GetRootDir().Append(
        base::StringPrintf("proc/%d", kFakeCrosvmProcessId));
    ASSERT_TRUE(base::CreateDirectory(crosvm_proc_));
    const char cmdline[] = "/usr/bin/crosvm\0--syslog-tag\0ARCVM(32)";
    ASSERT_TRUE(
        base::WriteFile(crosvm_proc_.Append("cmdline"),
                        std::string_view(cmdline, sizeof(cmdline) - 1)));

    // Set up the fake smaps contents for the fake crosvm process.
    base::flat_map<uint32_t, std::string> contents;
    contents[kFakeCrosvmProcessId] = kFakeSmapsContents;
    ON_CALL(*mock_executor(),
            GetProcessContents(mojom::Executor::ProcFile::kSmaps, _, _))
        .WillByDefault(base::test::RunOnceCallback<2>(contents));

    // Set up the fake concierge behaviors.
    // The following numbers were collected from a 8GiB hatch device by
    // adding LOG(INFO) in HandleGetVmInfo().
    vm_tools::concierge::GetVmInfoResponse vm_response;
    vm_response.set_success(true);
    vm_response.mutable_vm_info()->set_allocated_memory(7567572992);
    ON_CALL(*mock_context_.mock_concierge_proxy(), GetVmInfoAsync(_, _, _, _))
        .WillByDefault(base::test::RunOnceCallback<1>(vm_response));

    vm_tools::concierge::GetBalloonInfoResponse balloon_response;
    balloon_response.set_success(true);
    balloon_response.mutable_balloon_info()->set_balloon_size(5577539584);
    balloon_response.mutable_balloon_info()->set_available_memory(658972672);
    balloon_response.mutable_balloon_info()->set_free_memory(333049856);
    ON_CALL(*mock_context_.mock_concierge_proxy(),
            GetBalloonInfoAsync(_, _, _, _))
        .WillByDefault(base::test::RunOnceCallback<1>(balloon_response));

    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetRootDir().Append(kRelativeProcCpuInfoPath),
        kFakeCpuInfoNoTmeContent));
  }

  void CreateMkmteEnviroment() {
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
  MockContext& mock_context() { return mock_context_; }

  FakeMeminfoReader* fake_meminfo_reader() {
    return mock_context_.fake_meminfo_reader();
  }

  mojom::MemoryResultPtr FetchMemoryInfoSync() {
    base::test::TestFuture<mojom::MemoryResultPtr> future;
    FetchMemoryInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  base::FilePath crosvm_proc_;

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
};

// Test that memory info can be read when it exists.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoWithoutMemoryEncryption) {
  fake_meminfo_reader()->SetError(false);
  fake_meminfo_reader()->SetTotalMemoryKib(8000424);
  fake_meminfo_reader()->SetFreeMemoryKib(4544564);
  fake_meminfo_reader()->SetAvailableMemoryKib(5569176);

  fake_meminfo_reader()->SetBuffersKib(166684);
  fake_meminfo_reader()->SetPageCacheKib(1455512);
  fake_meminfo_reader()->SetSharedMemoryKib(283464);

  fake_meminfo_reader()->SetActiveMemoryKib(1718544);
  fake_meminfo_reader()->SetInactiveMemoryKib(970260);

  fake_meminfo_reader()->SetTotalSwapMemoryKib(16000844);
  fake_meminfo_reader()->SetFreeSwapMemoryKib(16000422);
  fake_meminfo_reader()->SetcachedSwapMemoryKib(132);

  fake_meminfo_reader()->SetTotalSlabMemoryKib(317140);
  fake_meminfo_reader()->SetReclaimableSlabMemoryKib(194160);
  fake_meminfo_reader()->SetUnreclaimableSlabMemoryKib(122980);

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // total_memory_kib should be computed with /proc/iomem with rounding.
  EXPECT_EQ(info->total_memory_kib, 8388608);
  EXPECT_EQ(info->free_memory_kib, 4544564);
  // available_memory_kib should be adjusted with the guest VM memory info
  // with ComputeAdjustedAvailable(), which is tested separately below,
  // rather than the exact value set by SetAvailableMemoryKib() above.
  EXPECT_EQ(info->available_memory_kib, 6791518);

  EXPECT_EQ(info->buffers_kib, 166684);
  EXPECT_EQ(info->page_cache_kib, 1455512);
  EXPECT_EQ(info->shared_memory_kib, 283464);

  EXPECT_EQ(info->active_memory_kib, 1718544);
  EXPECT_EQ(info->inactive_memory_kib, 970260);

  EXPECT_EQ(info->total_swap_memory_kib, 16000844);
  EXPECT_EQ(info->free_swap_memory_kib, 16000422);
  EXPECT_EQ(info->cached_swap_memory_kib, 132);

  EXPECT_EQ(info->total_slab_memory_kib, 317140);
  EXPECT_EQ(info->reclaimable_slab_memory_kib, 194160);
  EXPECT_EQ(info->unreclaimable_slab_memory_kib, 122980);

  EXPECT_EQ(info->page_faults_since_last_boot, 654654);
}

// Test that fetching memory info returns an error when /proc/meminfo is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcMeminfoFormattedIncorrectly) {
  fake_meminfo_reader()->SetError(true);

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
  EXPECT_EQ(result->get_error()->msg, "Error parsing /proc/meminfo");
}

// Test that fetching memory info uses /proc/meminfo when /proc/iomem is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcIomemFormattedIncorrectly) {
  fake_meminfo_reader()->SetError(false);
  fake_meminfo_reader()->SetTotalMemoryKib(8000424);

  // Override the ReadFile behavior with an incorrectly formatted file.
  ON_CALL(*mock_executor(), ReadFile(mojom::Executor::File::kProcIomem, _))
      .WillByDefault(base::test::RunOnceCallback<1>(
          kFakeIomemContentsIncorrectlyFormattedFile));

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // 8000424 in /proc/meminfo should be used instead.
  EXPECT_EQ(info->total_memory_kib, 8000424);
}

// Test that fetching memory info does not take ARCVM into account when
// RetrievePrimarySessionAsync failed.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoRetrievePrimarySessionFailed) {
  fake_meminfo_reader()->SetError(false);
  const int kAvailableMemory = 5569176;
  fake_meminfo_reader()->SetAvailableMemoryKib(kAvailableMemory);

  // Override the RetrievePriamarySessionAsync() behavior.
  brillo::ErrorPtr error =
      brillo::Error::Create(FROM_HERE, "", "", "random failure");
  ON_CALL(*mock_context().mock_session_manager_proxy(),
          RetrievePrimarySessionAsync(_, _, _))
      .WillByDefault(base::test::RunOnceCallback<1>(error.get()));

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // kAvailableMemory should be used as is.
  EXPECT_EQ(info->available_memory_kib, kAvailableMemory);
}

// Test that fetching memory info does not take ARCVM into account when
// GetVmInfo failed.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoGetVmInfoFailed) {
  fake_meminfo_reader()->SetError(false);
  const int kAvailableMemory = 5569176;
  fake_meminfo_reader()->SetAvailableMemoryKib(kAvailableMemory);

  // Override the GetVmInfoAsync() behavior.
  brillo::ErrorPtr error =
      brillo::Error::Create(FROM_HERE, "", "", "random failure");
  ON_CALL(*mock_context().mock_concierge_proxy(), GetVmInfoAsync(_, _, _, _))
      .WillByDefault(base::test::RunOnceCallback<2>(error.get()));

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // kAvailableMemory should be used as is.
  EXPECT_EQ(info->available_memory_kib, kAvailableMemory);
}

// Test that fetching memory info does not take ARCVM into account when
// GetBalloonInfo failed.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoGetBalloonInfoFailed) {
  fake_meminfo_reader()->SetError(false);
  const int kAvailableMemory = 5569176;
  fake_meminfo_reader()->SetAvailableMemoryKib(kAvailableMemory);

  // Override the GetBalloonInfoAsync() behavior.
  brillo::ErrorPtr error =
      brillo::Error::Create(FROM_HERE, "", "", "random failure");
  ON_CALL(*mock_context().mock_concierge_proxy(),
          GetBalloonInfoAsync(_, _, _, _))
      .WillByDefault(base::test::RunOnceCallback<2>(error.get()));

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // kAvailableMemory should be used as is.
  EXPECT_EQ(info->available_memory_kib, kAvailableMemory);
}

// Test that fetching memory info does not take ARCVM into account when
// crosvm process was not found.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoCrosvmNotFound) {
  fake_meminfo_reader()->SetError(false);
  const int kAvailableMemory = 5569176;
  fake_meminfo_reader()->SetAvailableMemoryKib(kAvailableMemory);

  // Delete the fake crosvm process directory.
  brillo::DeletePathRecursively(crosvm_proc_);

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // kAvailableMemory should be used as is.
  EXPECT_EQ(info->available_memory_kib, kAvailableMemory);
}

// Test that fetching memory info does not take ARCVM into account when
// the smaps file contents are formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoSmapsFormattedIncorrectly) {
  fake_meminfo_reader()->SetError(false);
  const int kAvailableMemory = 5569176;
  fake_meminfo_reader()->SetAvailableMemoryKib(kAvailableMemory);

  // Override the fake smaps contents for the fake crosvm process.
  base::flat_map<uint32_t, std::string> contents;
  contents[kFakeCrosvmProcessId] = "broken smaps contents";
  ON_CALL(*mock_executor(),
          GetProcessContents(mojom::Executor::ProcFile::kSmaps, _, _))
      .WillByDefault(base::test::RunOnceCallback<2>(contents));

  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetRootDir().Append(kRelativeVmStatPath), kFakeVmStatContents));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_memory_info());
  const auto& info = result->get_memory_info();
  // kAvailableMemory should be used as is.
  EXPECT_EQ(info->available_memory_kib, kAvailableMemory);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// exist.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoNoProcVmStat) {
  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/vmstat is
// formatted incorrectly.
TEST_F(MemoryFetcherTest, TestFetchMemoryInfoProcVmStatFormattedIncorrectly) {
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
      GetRootDir().Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedPgfault));

  auto result = FetchMemoryInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kParseError);
}

// Test to handle missing /sys/kernel/mm/mktme directory.
TEST_F(MemoryFetcherTest, MissingMktmeDirectory) {
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
            std::move(callback).Run(kTmeCapabilityMsrValue);
            return;
          case cpu_msr::kIA32TmeActivate:
            std::move(callback).Run(kTmeActivateMsrValue);
            return;
          default:
            std::move(callback).Run(std::nullopt);
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

TEST_F(MemoryFetcherTest, ComputeAdjustedAvailable) {
  GuestMemoryInfo guest;
  // The followings are roughly based on numbers collected from a 4GiB device.
  guest.balloon_size = 900000000;
  guest.allocated_memory = 3500000000;
  guest.available_memory = 1600000000;
  guest.free_memory = 400000000;
  guest.crosvm_rss = 500000000;
  guest.crosvm_swap = 1500000000;

  EXPECT_EQ(ComputeAdjustedAvailable(guest), 400000000);
}

}  // namespace
}  // namespace diagnostics
