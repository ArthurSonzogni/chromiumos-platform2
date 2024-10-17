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
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace mantis {
namespace {

constexpr char kDlcName[] = "ml-dlc-302a455f-5453-43fb-a6a1-d856e6fe6435";

using ::testing::Return;
using MantisAPIGetter = const MantisAPI* (*)();

class MantisServiceTest : public testing::Test {
 public:
  MantisServiceTest() : service_(raw_ref(shim_loader_)) {
    mojo::core::Init();

    service_.AddReceiver(service_remote_.BindNewPipeAndPassReceiver());
  }

  void SetupDlc() {
    auto dlc_path = base::FilePath("testdata").Append(kDlcName);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  MantisService service_;
  odml::OdmlShimLoaderMock shim_loader_;
  mojo::Remote<mojom::MantisService> service_remote_;
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

  EXPECT_FALSE(service_.IsProcessorNullForTesting());

  processor1.reset();
  processor2.reset();

  service_remote_.FlushForTesting();
  EXPECT_TRUE(service_.IsProcessorNullForTesting());
}

}  // namespace
}  // namespace mantis
