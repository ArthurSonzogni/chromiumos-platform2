// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <optional>
#include <string>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/uuid.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/fake/simple_fake_service_manager.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "base/test/gmock_callback_support.h"
#include "gmock/gmock.h"
#include "metrics/metrics_library_mock.h"
#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace mantis {
namespace {

constexpr char kDlcPrefix[] = "ml-dlc-";
constexpr char kDefaultDlcUUID[] = "302a455f-5453-43fb-a6a1-d856e6fe6435";

using ::testing::_;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using MantisAPIGetter = const MantisAPI* (*)();

class MantisServiceTest : public testing::Test {
 public:
  MantisServiceTest() {
    mojo::core::Init();

    service_ = std::make_unique<MantisService>(
        raw_ref(metrics_lib_), raw_ref(shim_loader_),
        raw_ref(safety_service_manager_));

    service_->AddReceiver(service_remote_.BindNewPipeAndPassReceiver());
  }

  void SetupDlc() {
    auto dlc_name = std::string(kDlcPrefix) + std::string(kDefaultDlcUUID);
    auto dlc_path = base::FilePath("testdata").Append(dlc_name);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  NiceMock<MetricsLibraryMock> metrics_lib_;
  odml::OdmlShimLoaderMock shim_loader_;
  std::unique_ptr<MantisService> service_;
  mojo::Remote<mojom::MantisService> service_remote_;
  cros_safety::SafetyServiceManagerMock safety_service_manager_;
};

TEST_F(MantisServiceTest, InitializeUnableToResolveGetMantisAPISymbol) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(nullptr));
  SetupDlc();

  EXPECT_CALL(metrics_lib_,
              SendBoolToUMA("Platform.MantisService.ModelLoaded", false));
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.LoadModel", _, _, _, _))
      .Times(0);

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::Uuid::ParseLowercase(kDefaultDlcUUID),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeUnableToGetMantisAPI) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(
          MantisAPIGetter([]() -> const MantisAPI* { return 0; }))));
  SetupDlc();

  EXPECT_CALL(metrics_lib_,
              SendBoolToUMA("Platform.MantisService.ModelLoaded", false));
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.LoadModel", _, _, _, _))
      .Times(0);

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::Uuid::ParseLowercase(kDefaultDlcUUID),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeFailedToDownloadShim) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(false));
  EXPECT_CALL(shim_loader_, InstallVerifiedShim)
      .WillOnce(base::test::RunOnceCallback<0>(false));
  EXPECT_CALL(shim_loader_, EnsureShimReady)
      .WillOnce(base::test::RunOnceCallback<0>(false));

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(), std::nullopt,
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceeds) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  SetupDlc();

  EXPECT_CALL(metrics_lib_,
              SendBoolToUMA("Platform.MantisService.ModelLoaded", false));
  EXPECT_CALL(metrics_lib_,
              SendTimeToUMA("Platform.MantisService.Latency.LoadModel",
                            Gt(base::Seconds(0)), _, _, _));

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::Uuid::ParseLowercase(kDefaultDlcUUID),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceedsWithEmptyDLC) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(), std::nullopt,
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceedsWithShimInstallation) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(false));
  EXPECT_CALL(shim_loader_, InstallVerifiedShim)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(), std::nullopt,
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceedsWithShimDownload) {
  EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(false));
  EXPECT_CALL(shim_loader_, InstallVerifiedShim)
      .WillOnce(base::test::RunOnceCallback<0>(false));
  EXPECT_CALL(shim_loader_, EnsureShimReady)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
      .WillOnce(base::test::RunOnceCallback<0>(true));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(), std::nullopt,
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, MultipleClients) {
  EXPECT_CALL(shim_loader_, IsShimReady).Times(2).WillRepeatedly(Return(true));
  // GetMantisAPI should only be called once
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<0>(true));
  SetupDlc();

  {
    InSequence s;
    EXPECT_CALL(metrics_lib_,
                SendBoolToUMA("Platform.MantisService.ModelLoaded", false));
    EXPECT_CALL(metrics_lib_,
                SendTimeToUMA("Platform.MantisService.Latency.LoadModel",
                              Gt(base::Seconds(0)), _, _, _));
    EXPECT_CALL(metrics_lib_,
                SendBoolToUMA("Platform.MantisService.ModelLoaded", true));
  }

  base::RunLoop run_loop_1;
  mojo::Remote<mojom::MantisProcessor> processor1;
  service_remote_->Initialize(
      mojo::NullRemote(), processor1.BindNewPipeAndPassReceiver(),
      base::Uuid::ParseLowercase(kDefaultDlcUUID),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop_1.Quit();
      }));
  run_loop_1.Run();

  base::RunLoop run_loop_2;
  mojo::Remote<mojom::MantisProcessor> processor2;
  service_remote_->Initialize(
      mojo::NullRemote(), processor2.BindNewPipeAndPassReceiver(),
      base::Uuid::ParseLowercase(kDefaultDlcUUID),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop_2.Quit();
      }));
  run_loop_2.Run();

  EXPECT_FALSE(service_->IsProcessorNullForTesting());

  processor1.reset();
  processor2.reset();

  service_remote_.FlushForTesting();
  EXPECT_TRUE(service_->IsProcessorNullForTesting());
}

}  // namespace
}  // namespace mantis
