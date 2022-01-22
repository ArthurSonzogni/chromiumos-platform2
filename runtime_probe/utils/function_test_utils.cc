// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/json/json_reader.h>

#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {

BaseFunctionTest::BaseFunctionTest() {
  SetTestRoot(mock_context()->root_dir());
}

BaseFunctionTest::~BaseFunctionTest() = default;

// static
std::vector<base::Value> BaseFunctionTest::CreateProbeResultFromJson(
    const std::string& str) {
  auto res = base::JSONReader::Read(str);
  CHECK(res.has_value());
  return std::move(*res).TakeList();
}

}  // namespace runtime_probe
