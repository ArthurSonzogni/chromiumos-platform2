// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/multi_function_runner.h"

#include <memory>

#include <base/json/json_reader.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class FakeProbeFunction : public ProbeFunction {
  using ProbeFunction::ProbeFunction;

 public:
  NAME_PROBE_FUNCTION("fake");

 private:
  DataType EvalImpl() const override { return {}; }
};

class MultiFunctionRunnerTest : public ::testing::Test {
 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
};

TEST_F(MultiFunctionRunnerTest, Success) {
  MultiFunctionRunner runner;
  runner.AddFunction(CreateFakeProbeFunction<FakeProbeFunction>(R"JSON(
      [{
        "key1": "key1"
      }]
    )JSON"));
  runner.AddFunction(CreateFakeProbeFunction<FakeProbeFunction>(R"JSON(
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
  )JSON");
  base::test::TestFuture<base::Value::List> future;
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
  )JSON");
  base::test::TestFuture<base::Value::List> future;
  runner.Run(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

}  // namespace
}  // namespace runtime_probe
