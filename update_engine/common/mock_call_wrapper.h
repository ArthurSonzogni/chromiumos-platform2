// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_CALL_WRAPPER_H_
#define UPDATE_ENGINE_COMMON_MOCK_CALL_WRAPPER_H_

#include "update_engine/common/call_wrapper_interface.h"

#include <gmock/gmock.h>

namespace chromeos_update_engine {

class MockCallWrapper : public CallWrapperInterface {
 public:
  MOCK_METHOD(int64_t,
              AmountOfFreeDiskSpace,
              (const base::FilePath& path),
              (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_CALL_WRAPPER_H_
