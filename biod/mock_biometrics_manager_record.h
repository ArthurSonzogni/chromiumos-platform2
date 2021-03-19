// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_BIOMETRICS_MANAGER_RECORD_H_
#define BIOD_MOCK_BIOMETRICS_MANAGER_RECORD_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "biod/biometrics_manager_record.h"

namespace biod {

class MockBiometricsManagerRecord : public BiometricsManagerRecord {
 public:
  MockBiometricsManagerRecord() = default;
  ~MockBiometricsManagerRecord() override = default;

  MOCK_METHOD(const std::string&, GetId, (), (const, override));
  MOCK_METHOD(const std::string&, GetUserId, (), (const, override));
  MOCK_METHOD(const std::string&, GetLabel, (), (const, override));
  MOCK_METHOD(void, SetLabel, (std::string label), (override));
  MOCK_METHOD(const std::vector<uint8_t>&, GetValidationVal, (), (const));
  MOCK_METHOD(bool, SupportsPositiveMatchSecret, (), (const, override));
};

}  //  namespace biod

#endif  // BIOD_MOCK_BIOMETRICS_MANAGER_RECORD_H_
