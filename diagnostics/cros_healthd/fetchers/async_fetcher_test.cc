// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/fetchers/async_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace mojom = chromeos::cros_healthd::mojom;

int destructor_called_times;

class FakeAsyncFetcher : public AsyncFetcherInterface<mojom::TimezoneResult> {
 public:
  using AsyncFetcherInterface::AsyncFetcherInterface;

  static std::vector<ResultCallback> callbacks;

  ~FakeAsyncFetcher() override { destructor_called_times++; }

 private:
  // Overrides AsyncFetcherInterface.
  void FetchImpl(ResultCallback callback) override {
    callbacks.push_back(std::move(callback));
  }
};

std::vector<FakeAsyncFetcher::ResultCallback> FakeAsyncFetcher::callbacks;

FakeAsyncFetcher::ResultCallback ExpectToBeCalled(
    FakeAsyncFetcher::ResultCallback callback) {
  return mojo::WrapCallbackWithDropHandler(
      std::move(callback), base::BindOnce([]() {
        EXPECT_TRUE(false) << "The callback was dropped without being called.";
      }));
}

FakeAsyncFetcher::ResultCallback ExpectResultIsErrorCallback() {
  return ExpectToBeCalled(base::BindLambdaForTesting(
      [](mojom::TimezoneResultPtr res) { EXPECT_TRUE(res->is_error()); }));
}

FakeAsyncFetcher::ResultCallback ExpectResultCallback(
    mojom::TimezoneResultPtr expected) {
  return ExpectToBeCalled(base::BindLambdaForTesting(
      [expected = std::move(expected)](mojom::TimezoneResultPtr res) {
        EXPECT_EQ(res, expected);
      }));
}

class AsyncFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override { destructor_called_times = 0; }
  void TearDown() override {
    FakeAsyncFetcher::callbacks.clear();
    // Wait for all task to be done.
    env_.RunUntilIdle();
  }

  base::test::SingleThreadTaskEnvironment env_;
  MockContext mock_context_;
  AsyncFetcher<FakeAsyncFetcher> fetcher_{&mock_context_};
};

TEST_F(AsyncFetcherTest, Fetch) {
  auto expected =
      mojom::TimezoneResult::NewTimezoneInfo(mojom::TimezoneInfo::New());
  fetcher_.Fetch(ExpectResultCallback(expected.Clone()));
  EXPECT_EQ(FakeAsyncFetcher::callbacks.size(), 1);
  std::move(FakeAsyncFetcher::callbacks[0]).Run(expected.Clone());
  // Destructor should not be called at this time.
  EXPECT_EQ(destructor_called_times, 0);
  env_.RunUntilIdle();
  EXPECT_EQ(destructor_called_times, 1);
}

TEST_F(AsyncFetcherTest, FetchMultiple) {
  std::vector<mojom::TimezoneResultPtr> expecteds;
  expecteds.push_back(mojom::TimezoneResult::NewError(
      mojom::ProbeError::New(mojom::ErrorType::kFileReadError, "0")));
  expecteds.push_back(mojom::TimezoneResult::NewError(
      mojom::ProbeError::New(mojom::ErrorType::kFileReadError, "1")));
  expecteds.push_back(mojom::TimezoneResult::NewError(
      mojom::ProbeError::New(mojom::ErrorType::kFileReadError, "2")));
  expecteds.push_back(mojom::TimezoneResult::NewError(
      mojom::ProbeError::New(mojom::ErrorType::kFileReadError, "3")));
  for (int i = 0; i < 4; ++i) {
    fetcher_.Fetch(ExpectResultCallback(expecteds[i].Clone()));
  }
  EXPECT_EQ(FakeAsyncFetcher::callbacks.size(), 4);
  // Test the callback being fulfilled in reverse order.
  for (int i = 3; i >= 0; --i) {
    std::move(FakeAsyncFetcher::callbacks[i]).Run(expecteds[i].Clone());
  }
}

TEST_F(AsyncFetcherTest, FetchDropCallback) {
  fetcher_.Fetch(ExpectResultIsErrorCallback());
  std::move(FakeAsyncFetcher::callbacks[0]);
}

}  // namespace
}  // namespace diagnostics
