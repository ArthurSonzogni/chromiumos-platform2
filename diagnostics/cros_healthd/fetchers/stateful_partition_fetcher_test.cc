// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/stateful_partition_fetcher.h"

#include <string>

#include <base/files/file_path.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spaced/proto_bindings/spaced.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs spaced.pb.h
#include <spaced/dbus-proxy-mocks.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;

const char kFakeMountSource[] = "/dev/mmcblk0p1";
const char kFakeFilesystem[] = "ext4";
const char kFakeMtabOpt[] = "rw 0 0";

class StatefulePartitionFetcherTest : public BaseFileTest {
 public:
  StatefulePartitionFetcherTest(const StatefulePartitionFetcherTest&) = delete;
  StatefulePartitionFetcherTest& operator=(
      const StatefulePartitionFetcherTest&) = delete;

 protected:
  StatefulePartitionFetcherTest() = default;

  void SetUp() override {
    // Populate fake mtab contents.
    const auto fake_content =
        std::string(kFakeMountSource) + " " +
        GetRootDir().Append(kStatefulPartitionPath).value() + " " +
        kFakeFilesystem + " " + kFakeMtabOpt;
    SetFile(kMtabPath, fake_content);
  }

  mojom::StatefulPartitionResultPtr FetchStatefulPartitionInfoSync() {
    base::test::TestFuture<mojom::StatefulPartitionResultPtr> future;
    FetchStatefulPartitionInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  void SetAvailableSpaceResponse(std::optional<int64_t> free_space_byte) {
    if (free_space_byte.has_value()) {
      ON_CALL(*mock_spaced_proxy(), GetFreeDiskSpaceAsync(_, _, _, _))
          .WillByDefault(
              base::test::RunOnceCallback<1>(free_space_byte.value()));
    } else {
      error_ = brillo::Error::Create(FROM_HERE, "", "", "");
      ON_CALL(*mock_spaced_proxy(), GetFreeDiskSpaceAsync(_, _, _, _))
          .WillByDefault(base::test::RunOnceCallback<2>(error_.get()));
    }
  }

  void SetTotalSpaceResponse(std::optional<int64_t> total_space_byte) {
    if (total_space_byte.has_value()) {
      ON_CALL(*mock_spaced_proxy(), GetTotalDiskSpaceAsync(_, _, _, _))
          .WillByDefault(
              base::test::RunOnceCallback<1>(total_space_byte.value()));
    } else {
      error_ = brillo::Error::Create(FROM_HERE, "", "", "");
      ON_CALL(*mock_spaced_proxy(), GetTotalDiskSpaceAsync(_, _, _, _))
          .WillByDefault(base::test::RunOnceCallback<2>(error_.get()));
    }
  }

  org::chromium::SpacedProxyMock* mock_spaced_proxy() {
    return mock_context_.mock_spaced_proxy();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  brillo::ErrorPtr error_;
};

TEST_F(StatefulePartitionFetcherTest, Success) {
  SetAvailableSpaceResponse(15658385408);
  SetTotalSpaceResponse(20222021632);

  const auto result = FetchStatefulPartitionInfoSync();
  ASSERT_TRUE(result->is_partition_info());
  EXPECT_EQ(result->get_partition_info()->available_space, 15658385408);
  EXPECT_EQ(result->get_partition_info()->total_space, 20222021632);
  EXPECT_EQ(result->get_partition_info()->filesystem, kFakeFilesystem);
  EXPECT_EQ(result->get_partition_info()->mount_source, kFakeMountSource);
}

TEST_F(StatefulePartitionFetcherTest, GetAvailableSpaceError) {
  SetAvailableSpaceResponse(std::nullopt);
  SetTotalSpaceResponse(20222021632);

  const auto result = FetchStatefulPartitionInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
  EXPECT_EQ(result->get_error()->msg,
            "Failed to collect available space of stateful partition");
}

TEST_F(StatefulePartitionFetcherTest, GetTotalSpaceError) {
  SetAvailableSpaceResponse(15658385408);
  SetTotalSpaceResponse(std::nullopt);

  const auto result = FetchStatefulPartitionInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
  EXPECT_EQ(result->get_error()->msg,
            "Failed to collect total space of stateful partition");
}

TEST_F(StatefulePartitionFetcherTest, SpacedError) {
  const auto result = FetchStatefulPartitionInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
  EXPECT_EQ(result->get_error()->msg,
            "Failed to collect stateful partition info from spaced");
}

TEST_F(StatefulePartitionFetcherTest, NoMtabFileError) {
  SetAvailableSpaceResponse(15658385408);
  SetTotalSpaceResponse(20222021632);
  UnsetPath(kMtabPath);

  const auto result = FetchStatefulPartitionInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kSystemUtilityError);
  EXPECT_EQ(result->get_error()->msg,
            "Failed to collect stateful partition info from mtab");
}

}  // namespace
}  // namespace diagnostics
