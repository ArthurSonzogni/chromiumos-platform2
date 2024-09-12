// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/service.h"

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mantis/processor.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace mantis {
namespace {

using ::on_device_model::mojom::LoadModelResult;
using ::testing::Return;
using MantisAPIGetter = const MantisAPI* (*)();

class MantisServiceTest : public testing::Test {
 public:
  MantisServiceTest() : service_(raw_ref(shim_loader_)) { mojo::core::Init(); }

 protected:
  base::test::TaskEnvironment task_environment_;
  MantisService service_;
  odml::OdmlShimLoaderMock shim_loader_;
};

TEST_F(MantisServiceTest, InitializeUnableToResolveGetMantisAPISymbol) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(nullptr));

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_.Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        EXPECT_EQ(result, LoadModelResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeUnableToGetMantisAPI) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(
          MantisAPIGetter([]() -> const MantisAPI* { return 0; }))));

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_.Initialize(
      mojo::NullRemote(), processor.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](LoadModelResult result) {
        EXPECT_EQ(result, LoadModelResult::kFailedToLoadLibrary);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(MantisServiceTest, InitializeSucceeds) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("GetMantisAPI"))
      .WillOnce(Return(reinterpret_cast<void*>(MantisAPIGetter(
          []() -> const MantisAPI* { return fake::GetMantisApi(); }))));

  base::RunLoop run_loop;
  mojo::Remote<mojom::MantisProcessor> processor;
  service_.Initialize(mojo::NullRemote(),
                      processor.BindNewPipeAndPassReceiver(),
                      base::BindLambdaForTesting([&](LoadModelResult result) {
                        EXPECT_EQ(result, LoadModelResult::kSuccess);
                        run_loop.Quit();
                      }));
  run_loop.Run();
}

}  // namespace
}  // namespace mantis
