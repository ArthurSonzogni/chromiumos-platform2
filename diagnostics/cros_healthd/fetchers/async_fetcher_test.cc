// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/check.h>
#include <base/test/bind.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/callback_helpers.h>

#include "diagnostics/cros_healthd/fetchers/async_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {
namespace {

namespace mojom = chromeos::cros_healthd::mojom;

class FakeAsyncFetcher : public AsyncFetcher<mojom::TimezoneResult> {
 public:
  using AsyncFetcher::AsyncFetcher;

  // Overrides AsyncFetcher.
  void FetchImpl(ResultCallback callback) override {
    CHECK(!callback_) << "Call twice before the last call finished.";
    callback_ = std::move(callback);
  }

  void FulfillCallback(mojom::TimezoneResultPtr result) {
    CHECK(callback_) << "Callback is not set.";
    std::move(callback_).Run(std::move(result));
    callback_.Reset();
  }

  void DropCallback() {
    CHECK(callback_) << "Callback is not set.";
    callback_.Reset();
  }

 private:
  ResultCallback callback_;
};

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
  MockContext mock_context;
  FakeAsyncFetcher fetcher{&mock_context};
};

TEST_F(AsyncFetcherTest, Fetch) {
  auto expected =
      mojom::TimezoneResult::NewTimezoneInfo(mojom::TimezoneInfo::New());
  fetcher.Fetch(ExpectResultCallback(expected.Clone()));
  fetcher.FulfillCallback(expected.Clone());
}

TEST_F(AsyncFetcherTest, FetchMultiple) {
  auto expected =
      mojom::TimezoneResult::NewTimezoneInfo(mojom::TimezoneInfo::New());
  fetcher.Fetch(ExpectResultCallback(expected.Clone()));
  fetcher.Fetch(ExpectResultCallback(expected.Clone()));
  fetcher.Fetch(ExpectResultCallback(expected.Clone()));
  fetcher.Fetch(ExpectResultCallback(expected.Clone()));
  fetcher.FulfillCallback(expected.Clone());
}

TEST_F(AsyncFetcherTest, FetchDropCallback) {
  fetcher.Fetch(ExpectResultIsErrorCallback());
  fetcher.DropCallback();
}

}  // namespace
}  // namespace diagnostics
