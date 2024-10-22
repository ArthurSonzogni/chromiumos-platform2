// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/fake/simple_fake_service_manager.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/mantis/fake/fake_cros_safety_service.h"
#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mantis/mock_cloud_safety_session.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace mantis {
namespace {

constexpr char kDlcName[] = "ml-dlc-302a455f-5453-43fb-a6a1-d856e6fe6435";
constexpr uint32_t kMantisUid = 123;

using ::testing::Return;
using MantisAPIGetter = const MantisAPI* (*)();

class MantisServiceTest : public testing::Test {
 public:
  MantisServiceTest() {
    mojo::core::Init();

    mojo_service_manager_ = std::make_unique<
        chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>();
    remote_service_manager_.Bind(
        mojo_service_manager_->AddNewPipeAndPassRemote(kMantisUid));

    service_ = std::make_unique<MantisService>(
        raw_ref(shim_loader_), raw_ref(remote_service_manager_));

    service_->AddReceiver(service_remote_.BindNewPipeAndPassReceiver());

    safety_service_provider_impl_ =
        std::make_unique<fake::FakeCrosSafetyServiceProviderImpl>(
            remote_service_manager_, raw_ref(cloud_safety_session_));
  }

  void SetupDlc() {
    auto dlc_path = base::FilePath("testdata").Append(kDlcName);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  odml::OdmlShimLoaderMock shim_loader_;
  mantis::MockCloudSafetySession cloud_safety_session_;
  std::unique_ptr<chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>
      mojo_service_manager_;
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      remote_service_manager_;
  std::unique_ptr<MantisService> service_;
  mojo::Remote<mojom::MantisService> service_remote_;
  std::unique_ptr<fake::FakeCrosSafetyServiceProviderImpl>
      safety_service_provider_impl_;
};

TEST_F(MantisServiceTest, InitializeUnableToResolveGetMantisAPISymbol) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(nullptr));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeUnableToGetMantisAPI) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(
          MantisAPIGetter([]() -> const MantisAPI* { return 0; }))));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceeds) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  SetupDlc();

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_remote_->Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, MultipleClients) {
  EXPECT_CALL(shim_loader_, IsShimReady())
      .Times(2)
      .WillRepeatedly(Return(true));
  // GetMantisAPI should only be called once
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));
  SetupDlc();

  base::RunLoop run_loop_1;
  mojo::Remote<mojom::MantisProcessor> processor1;
  service_remote_->Initialize(
      mojo::NullRemote(), processor1.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](mojom::InitializeResult result) {
        EXPECT_EQ(result, mojom::InitializeResult::kSuccess);
        run_loop_1.Quit();
      }));
  run_loop_1.Run();

  base::RunLoop run_loop_2;
  mojo::Remote<mojom::MantisProcessor> processor2;
  service_remote_->Initialize(
      mojo::NullRemote(), processor2.BindNewPipeAndPassReceiver(),
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
