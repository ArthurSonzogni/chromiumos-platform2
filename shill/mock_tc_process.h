// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_TC_PROCESS_H_
#define SHILL_MOCK_TC_PROCESS_H_

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/tc_process.h"

namespace shill {

class MockTCProcess : public TCProcess {
 public:
  MockTCProcess();
  ~MockTCProcess() override;
};

class MockTCProcessFactory : public TCProcessFactory {
 public:
  MockTCProcessFactory();
  ~MockTCProcessFactory() override;

  MOCK_METHOD(std::unique_ptr<TCProcess>,
              Create,
              (const std::vector<std::string>&,
               TCProcess::ExitCallback,
               net_base::ProcessManager*),
              (override));
};

}  // namespace shill
#endif  // SHILL_MOCK_TC_PROCESS_H_
