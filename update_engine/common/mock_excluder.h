// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_APP_EXCLUDER_H_
#define UPDATE_ENGINE_COMMON_MOCK_APP_EXCLUDER_H_

#include "update_engine/common/excluder_interface.h"

#include <string>

#include <gmock/gmock.h>

namespace chromeos_update_engine {

class MockExcluder : public ExcluderInterface {
 public:
  MOCK_METHOD(bool, Exclude, (const std::string&), (override));
  MOCK_METHOD(bool, IsExcluded, (const std::string&), (override));
  MOCK_METHOD(bool, Reset, (), (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_APP_EXCLUDER_H_
