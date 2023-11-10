// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_MOCK_PROCESS_EXECUTOR_H_
#define ROUTING_SIMULATOR_MOCK_PROCESS_EXECUTOR_H_

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "routing-simulator/process_executor.h"

namespace routing_simulator {
class MockProcessExecutor : public ProcessExecutor {
 public:
  MockProcessExecutor();
  MockProcessExecutor(const MockProcessExecutor&) = delete;
  MockProcessExecutor& operator=(const MockProcessExecutor&) = delete;

  ~MockProcessExecutor();

  MOCK_METHOD(std::optional<std::string>,
              RunAndGetStdout,
              (const base::FilePath& program,
               const std::vector<std::string>& args),
              (override));
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_MOCK_PROCESS_EXECUTOR_H_
