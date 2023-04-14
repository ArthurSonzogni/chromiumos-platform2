// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>

#include "runtime_probe/functions/mmc_host.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class MmcHostFunctionTest : public BaseFunctionTest {};

TEST_F(MmcHostFunctionTest, ProbeMmcHost) {
  base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function = CreateProbeFunction<MmcHostFunction>(probe_statement);

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

}  // namespace
}  // namespace runtime_probe
