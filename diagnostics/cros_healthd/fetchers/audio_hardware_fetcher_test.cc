// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/audio_hardware_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

class AudioHardwareFetcherTest : public BaseFileTest {
 protected:
  void SetUp() override { SetTestRoot(mock_context_.root_dir()); }

  mojom::AudioHardwareResultPtr FetchAudioHardwareInfoSync() {
    base::RunLoop run_loop;
    mojom::AudioHardwareResultPtr result;
    FetchAudioHardwareInfo(
        &mock_context_,
        base::BindLambdaForTesting([&](mojom::AudioHardwareResultPtr response) {
          result = std::move(response);
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

 private:
  base::test::SingleThreadTaskEnvironment env_;
  MockContext mock_context_;
};

}  // namespace
}  // namespace diagnostics
