// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = chromeos::cros_healthd::mojom;

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

// Saves |response| to |response_destination|.
void OnGetDisplayInfoResponseReceived(
    mojo_ipc::DisplayResultPtr* response_destination,
    base::RepeatingClosure quit_closure,
    mojo_ipc::DisplayResultPtr response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}

class DisplayFetcherTest : public ::testing::Test {
 protected:
  DisplayFetcherTest() = default;

  mojo_ipc::DisplayResultPtr FetchDisplayInfo() {
    base::RunLoop run_loop;
    mojo_ipc::DisplayResultPtr result;
    display_fetcher_.FetchDisplayInfo(base::BindOnce(
        &OnGetDisplayInfoResponseReceived, &result, run_loop.QuitClosure()));

    run_loop.Run();

    return result;
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  DisplayFetcher display_fetcher_{&mock_context_};
};

TEST_F(DisplayFetcherTest, EmbeddedDisplayInfo) {
  auto display_result = FetchDisplayInfo();

  ASSERT_TRUE(display_result->is_display_info());
  const auto& display_info = display_result->get_display_info();

  const auto& edp_info = display_info->edp_info;
  EXPECT_TRUE(edp_info->privacy_screen_supported);
  EXPECT_FALSE(edp_info->privacy_screen_enabled);
}

}  // namespace
}  // namespace diagnostics
