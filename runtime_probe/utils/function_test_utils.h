// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_FUNCTION_TEST_UTILS_H_
#define RUNTIME_PROBE_UTILS_FUNCTION_TEST_UTILS_H_

#include <string>
#include <vector>

#include <base/values.h>

#include "runtime_probe/system/context_mock_impl.h"
#include "runtime_probe/utils/file_test_utils.h"

namespace runtime_probe {

// A helper class for creating probe function related unittest.
class BaseFunctionTest : public BaseFileTest {
 protected:
  BaseFunctionTest();
  BaseFunctionTest(const BaseFunctionTest&) = delete;
  BaseFunctionTest& operator=(const BaseFunctionTest&) = delete;
  ~BaseFunctionTest();

 protected:
  // Helper function to create the expected probe result form a json string.
  static std::vector<base::Value> CreateProbeResultFromJson(
      const std::string& str);

  ContextMockImpl* mock_context() { return &mock_context_; }

 private:
  ContextMockImpl mock_context_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_FUNCTION_TEST_UTILS_H_
