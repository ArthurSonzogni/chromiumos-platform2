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
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/i18n/mock_translator.h"
#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace mantis {
namespace {

constexpr char kDlcPrefix[] = "ml-dlc-";
constexpr char kDefaultDlcUUID[] = "302a455f-5453-43fb-a6a1-d856e6fe6435";

using ::base::test::TestFuture;
using ::testing::_;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using MantisAPIGetter = const MantisAPI* (*)();

class MockProgressObserver : public mojom::PlatformModelProgressObserver {
 public:
  MOCK_METHOD(void, Progress, (double progress), (override));

  mojo::PendingRemote<mojom::PlatformModelProgressObserver>
  BindNewPipeAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<mojom::PlatformModelProgressObserver> receiver_{this};
};

class MantisServiceTest : public testing::Test {
 public:
  MantisServiceTest() {
    mojo::core::Init();

    service_ = std::make_unique<MantisService>(
        raw_ref(metrics_lib_), raw_ref(shim_loader_),
        raw_ref(safety_service_manager_), raw_ref(translator_));

    service_->AddReceiver(service_remote_.BindNewPipeAndPassReceiver());
  }

  void SetupDlc() {
    auto dlc_name = std::string(kDlcPrefix) + std::string(kDefaultDlcUUID);
    auto dlc_path = base::FilePath("testdata").Append(dlc_name);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
    ON_CALL(translator_, IsDlcDownloaded).WillByDefault(Return(true));
  }

  void SetupShimForI18nDlcTest() {
    EXPECT_CALL(shim_loader_, IsShimReady).WillOnce(Return(false));
    EXPECT_CALL(shim_loader_, InstallVerifiedShim)
        .WillOnce(base::test::RunOnceCallback<0>(false));
    EXPECT_CALL(shim_loader_, EnsureShimReady)
        .WillOnce(base::test::RunOnceCallback<0>(true));

    // Optional calls, depends on whether i18n DLC succeed or not.
    ON_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
        .WillByDefault(Return(reinterpret_cast<void*>(MantisAPIGetter(
            []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
    ON_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
        .WillByDefault(base::test::RunOnceCallback<0>(true));
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  NiceMock<MetricsLibraryMock> metrics_lib_;
  odml::OdmlShimLoaderMock shim_loader_;
  std::unique_ptr<MantisService> service_;
  mojo::Remote<mojom::MantisService> service_remote_;
  cros_safety::SafetyServiceManagerMock safety_service_manager_;
  i18n::MockTranslator translator_;
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

TEST_F(MantisServiceTest, I18nDLCIsDownloaded) {
  SetupShimForI18nDlcTest();
  EXPECT_CALL(translator_, IsDlcDownloaded).WillRepeatedly(Return(true));
  EXPECT_CALL(translator_, DownloadDlc).Times(0);
  SetupDlc();

  MockProgressObserver progress_observer;
  {
    InSequence s;
    EXPECT_CALL(progress_observer, Progress(0));
    // First language
    EXPECT_CALL(progress_observer, Progress(0.95));
    // Second language
    EXPECT_CALL(progress_observer, Progress(0.975));
    // Third language
    EXPECT_CALL(progress_observer, Progress(1.0));
  }

  mojo::Remote<mojom::MantisProcessor> processor;
  TestFuture<mojom::InitializeResult> result_future;
  service_remote_->Initialize(progress_observer.BindNewPipeAndPassRemote(),
                              processor.BindNewPipeAndPassReceiver(),
                              base::Uuid::ParseLowercase(kDefaultDlcUUID),
                              result_future.GetCallback());
  service_remote_.FlushForTesting();

  EXPECT_EQ(result_future.Take(), mojom::InitializeResult::kSuccess);
}

TEST_F(MantisServiceTest, I18nDLCProgressIsSequential) {
  SetupShimForI18nDlcTest();
  EXPECT_CALL(translator_, IsDlcDownloaded).WillRepeatedly(Return(false));
  EXPECT_CALL(translator_, DownloadDlc)
      .WillRepeatedly([](const i18n::LangPair& lang_pair,
                         base::OnceCallback<void(bool)> callback,
                         odml::DlcProgressCallback progress) {
        // Send 2 progress for each language
        progress.Run(0.5);
        progress.Run(1.0);
        std::move(callback).Run(true);
      });
  SetupDlc();

  MockProgressObserver progress_observer;
  {
    InSequence s;
    EXPECT_CALL(progress_observer, Progress(0));
    // First language
    EXPECT_CALL(progress_observer, Progress(0.9375));
    EXPECT_CALL(progress_observer, Progress(0.95));
    // Second language
    EXPECT_CALL(progress_observer, Progress(0.9625));
    EXPECT_CALL(progress_observer, Progress(0.975));
    // Third language
    EXPECT_CALL(progress_observer, Progress(0.9875));
    EXPECT_CALL(progress_observer, Progress(1.0));
  }

  mojo::Remote<mojom::MantisProcessor> processor;
  TestFuture<mojom::InitializeResult> result_future;
  service_remote_->Initialize(progress_observer.BindNewPipeAndPassRemote(),
                              processor.BindNewPipeAndPassReceiver(),
                              base::Uuid::ParseLowercase(kDefaultDlcUUID),
                              result_future.GetCallback());
  service_remote_.FlushForTesting();

  EXPECT_EQ(result_future.Take(), mojom::InitializeResult::kSuccess);
}

TEST_F(MantisServiceTest, I18nDLCDownloadFailed) {
  SetupShimForI18nDlcTest();
  EXPECT_CALL(translator_, IsDlcDownloaded).WillRepeatedly(Return(false));
  EXPECT_CALL(translator_, DownloadDlc)
      // Success for the first language but failed at second
      .WillOnce([](const i18n::LangPair& lang_pair,
                   base::OnceCallback<void(bool)> callback,
                   odml::DlcProgressCallback progress) {
        progress.Run(1.0);
        std::move(callback).Run(true);
      })
      .WillOnce(base::test::RunOnceCallback<1>(false));
  SetupDlc();

  MockProgressObserver progress_observer;
  {
    InSequence s;
    EXPECT_CALL(progress_observer, Progress(0));
    // Still got progress for the first successful language
    EXPECT_CALL(progress_observer, Progress(0.95));
    // No progress for the second failed language
    EXPECT_CALL(progress_observer, Progress(0.975)).Times(0);
  }

  mojo::Remote<mojom::MantisProcessor> processor;
  TestFuture<mojom::InitializeResult> result_future;
  service_remote_->Initialize(progress_observer.BindNewPipeAndPassRemote(),
                              processor.BindNewPipeAndPassReceiver(),
                              base::Uuid::ParseLowercase(kDefaultDlcUUID),
                              result_future.GetCallback());
  service_remote_.FlushForTesting();

  EXPECT_EQ(result_future.Take(),
            mojom::InitializeResult::kFailedToLoadLibrary);
}

}  // namespace
}  // namespace mantis
