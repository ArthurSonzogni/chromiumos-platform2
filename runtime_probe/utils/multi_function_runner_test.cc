// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/multi_function_runner.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/json/json_reader.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class LifetimeCheckProbeFunction : public ProbeFunction {
 public:
  explicit LifetimeCheckProbeFunction(bool* alive_after_eval_callback)
      : alive_after_eval_callback_(alive_after_eval_callback) {}
  ~LifetimeCheckProbeFunction() override { is_alive_ = false; }

  NAME_PROBE_FUNCTION("lifetime_check");

 private:
  DataType EvalImpl() const override { return DataType{}; }

  void EvalAsyncImpl(
      base::OnceCallback<void(DataType)> callback) const override {
    base::ListValue result;
    result.Append(base::DictValue().Set("key", "val"));
    std::move(callback).Run(std::move(result));
    // Verify |this| has not been destroyed synchronously inside |callback|.
    *alive_after_eval_callback_ = is_alive_;
  }

  bool is_alive_ = true;
  bool* const alive_after_eval_callback_;
};

class MultiFunctionRunnerTest : public ::testing::Test {
 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
};

TEST_F(MultiFunctionRunnerTest, Success) {
  MultiFunctionRunner runner;
  runner.AddFunction(std::make_unique<FakeProbeFunction>(R"JSON(
      [{
        "key1": "key1"
      }]
    )JSON"));
  runner.AddFunction(std::make_unique<FakeProbeFunction>(R"JSON(
      [{
        "key2": "key2"
      },
      {
        "key3": "key3"
      }]
    )JSON"));

  EXPECT_TRUE(runner.IsValid());
  auto ans = base::JSONReader::Read(R"JSON(
    [{
      "key1": "key1"
    },
    {
      "key2": "key2"
    },
    {
      "key3": "key3"
    }]
  )JSON",
                                    base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  base::test::TestFuture<base::ListValue> future;
  runner.Run(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

TEST_F(MultiFunctionRunnerTest, ProberInitilizationFailed) {
  MultiFunctionRunner runner;
  runner.AddFunction(nullptr);

  EXPECT_FALSE(runner.IsValid());
}

TEST_F(MultiFunctionRunnerTest, ProbeEmptyResults) {
  MultiFunctionRunner runner;

  EXPECT_TRUE(runner.IsValid());
  auto ans = base::JSONReader::Read(R"JSON(
    []
  )JSON",
                                    base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  base::test::TestFuture<base::ListValue> future;
  runner.Run(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

TEST_F(MultiFunctionRunnerTest, RunDestroyRunnerInCallback) {
  bool first_alive_after_eval = false;
  bool second_alive_after_eval = false;
  auto runner = std::make_unique<MultiFunctionRunner>();
  runner->AddFunction(
      std::make_unique<LifetimeCheckProbeFunction>(&first_alive_after_eval));
  runner->AddFunction(
      std::make_unique<LifetimeCheckProbeFunction>(&second_alive_after_eval));

  base::test::TestFuture<base::ListValue> future;
  auto* runner_ptr = runner.get();
  runner_ptr->Run(base::BindOnce(
      [](std::unique_ptr<MultiFunctionRunner> owned_runner,
         base::OnceCallback<void(base::ListValue)> cb, base::ListValue result) {
        owned_runner.reset();
        std::move(cb).Run(std::move(result));
      },
      std::move(runner), future.GetCallback()));
  EXPECT_EQ(future.Get().size(), 2u);
  EXPECT_TRUE(first_alive_after_eval);
  EXPECT_TRUE(second_alive_after_eval);
}

TEST_F(MultiFunctionRunnerTest, RunEmptyDestroyRunnerInCallback) {
  auto runner = std::make_unique<MultiFunctionRunner>();

  base::test::TestFuture<base::ListValue> future;
  auto* runner_ptr = runner.get();
  runner_ptr->Run(base::BindOnce(
      [](std::unique_ptr<MultiFunctionRunner> owned_runner,
         base::OnceCallback<void(base::ListValue)> cb, base::ListValue result) {
        owned_runner.reset();
        std::move(cb).Run(std::move(result));
      },
      std::move(runner), future.GetCallback()));
  EXPECT_TRUE(future.Get().empty());
}

}  // namespace
}  // namespace runtime_probe
