// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/printscan_tool.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/task/single_thread_task_runner.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <lorgnette-client-test/lorgnette/dbus-proxy-mocks.h>
#include <printscanmgr/proto_bindings/printscanmgr_service.pb.h>

#include "printscanmgr/executor/mock_executor.h"
#include "printscanmgr/mojom/executor.mojom.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace printscanmgr {

namespace {

const char kCupsDebugPath[] = "run/cups/debug/debug-flag";
const char kIppusbDebugPath[] = "run/ippusb/debug/debug-flag";

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

lorgnette::SetDebugConfigRequest ConstructSetDebugConfigRequest(bool enabled) {
  lorgnette::SetDebugConfigRequest request;
  request.set_enabled(enabled);
  return request;
}

}  // namespace

class PrintscanToolTest : public testing::Test {
 protected:
  base::ScopedTempDir temp_dir_;
  StrictMock<MockExecutor> mock_executor_;
  std::unique_ptr<PrintscanTool> printscan_tool_;
  // Owned by `printscan_tool_`.
  StrictMock<org::chromium::lorgnette::ManagerProxyMock>* lorgnette_proxy_mock_;

  void SetUp() override {
    // Initialize IPC support for Mojo.
    ipc_support_ = std::make_unique<::mojo::core::ScopedIPCSupport>(
        base::SingleThreadTaskRunner::
            GetCurrentDefault() /* io_thread_task_runner */,
        ::mojo::core::ScopedIPCSupport::ShutdownPolicy::
            CLEAN /* blocking shutdown */);

    // Create directories we expect PrintscanTool to interact with.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::SetPosixFilePermissions(temp_dir_.GetPath(), 0755));
    ASSERT_TRUE(
        base::CreateDirectory(temp_dir_.GetPath().Append("run/cups/debug/")));
    ASSERT_TRUE(
        base::CreateDirectory(temp_dir_.GetPath().Append("run/ippusb/debug/")));

    // Prepare default responses for the mock Mojo method.
    ON_CALL(mock_executor_, RestartUpstartJob(_, _))
        .WillByDefault(WithArg<1>(
            Invoke([](mojom::Executor::RestartUpstartJobCallback callback) {
              std::move(callback).Run(/*success=*/true, /*errorMsg=*/"");
            })));

    auto lorgnette_proxy_mock = std::make_unique<
        StrictMock<org::chromium::lorgnette::ManagerProxyMock>>();
    lorgnette_proxy_mock_ = lorgnette_proxy_mock.get();

    // Prepare default responses for the mock D-Bus methods.
    ON_CALL(*lorgnette_proxy_mock_, SetDebugConfig(_, _, _, _))
        .WillByDefault(DoAll(
            WithArg<1>(Invoke([](lorgnette::SetDebugConfigResponse* response) {
              ASSERT_NE(response, nullptr);
              response->set_success(true);
            })),
            Return(true)));

    // Initialize PrintscanTool with a fake root for testing.
    remote_.Bind(mock_executor_.pending_remote());
    printscan_tool_ = PrintscanTool::CreateAndInitForTesting(
        remote_.get(), temp_dir_.GetPath(), std::move(lorgnette_proxy_mock));
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  mojo::Remote<mojom::Executor> remote_;
};

TEST_F(PrintscanToolTest, SetNoCategories) {
  // Test disabling debugging when it is already off.
  EXPECT_CALL(mock_executor_, RestartUpstartJob(mojom::UpstartJob::kCupsd, _));
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(false)),
                             _, _, _));
  PrintscanDebugSetCategoriesRequest request;

  auto response = printscan_tool_->DebugSetCategories(request);

  EXPECT_TRUE(response.result());
  EXPECT_FALSE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_FALSE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));
}

TEST_F(PrintscanToolTest, SetPrintingCategory) {
  // Test starting printing debugging only.
  EXPECT_CALL(mock_executor_, RestartUpstartJob(mojom::UpstartJob::kCupsd, _));
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(false)),
                             _, _, _));
  PrintscanDebugSetCategoriesRequest request;
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_PRINTING);

  auto response = printscan_tool_->DebugSetCategories(request);

  EXPECT_TRUE(response.result());
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));
}

TEST_F(PrintscanToolTest, SetScanningCategory) {
  // Test starting scanning debugging only.
  EXPECT_CALL(mock_executor_, RestartUpstartJob(mojom::UpstartJob::kCupsd, _));
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(true)),
                             _, _, _));
  PrintscanDebugSetCategoriesRequest request;
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_SCANNING);

  auto response = printscan_tool_->DebugSetCategories(request);

  EXPECT_TRUE(response.result());
  EXPECT_FALSE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));
}

TEST_F(PrintscanToolTest, SetAllCategories) {
  // Test starting all debugging.
  EXPECT_CALL(mock_executor_, RestartUpstartJob(mojom::UpstartJob::kCupsd, _));
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(true)),
                             _, _, _));
  PrintscanDebugSetCategoriesRequest request;
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_PRINTING);
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_SCANNING);

  auto response = printscan_tool_->DebugSetCategories(request);

  EXPECT_TRUE(response.result());
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));
}

TEST_F(PrintscanToolTest, ResetCategories) {
  // Test starting all debugging.
  EXPECT_CALL(mock_executor_, RestartUpstartJob(mojom::UpstartJob::kCupsd, _))
      .Times(2);
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(true)),
                             _, _, _));
  EXPECT_CALL(*lorgnette_proxy_mock_,
              SetDebugConfig(EqualsProto(ConstructSetDebugConfigRequest(false)),
                             _, _, _));
  PrintscanDebugSetCategoriesRequest request;
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_PRINTING);
  request.add_categories(
      PrintscanDebugSetCategoriesRequest::DEBUG_LOG_CATEGORY_SCANNING);

  auto response = printscan_tool_->DebugSetCategories(request);

  EXPECT_TRUE(response.result());
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_TRUE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));

  // Test stopping all debugging.
  PrintscanDebugSetCategoriesRequest empty_request;

  response = printscan_tool_->DebugSetCategories(empty_request);

  EXPECT_TRUE(response.result());
  EXPECT_FALSE(base::PathExists(temp_dir_.GetPath().Append(kCupsDebugPath)));
  EXPECT_FALSE(base::PathExists(temp_dir_.GetPath().Append(kIppusbDebugPath)));
}

}  // namespace printscanmgr
