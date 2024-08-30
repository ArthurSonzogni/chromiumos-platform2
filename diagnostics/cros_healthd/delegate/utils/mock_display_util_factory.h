// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_MOCK_DISPLAY_UTIL_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_MOCK_DISPLAY_UTIL_FACTORY_H_

#include <memory>

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/delegate/utils/display_util.h"
#include "diagnostics/cros_healthd/delegate/utils/display_util_factory.h"

namespace diagnostics {

class MockDisplayUtilFactory : public DisplayUtilFactory {
 public:
  MOCK_METHOD(std::unique_ptr<DisplayUtil>, Create, (), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_MOCK_DISPLAY_UTIL_FACTORY_H_
