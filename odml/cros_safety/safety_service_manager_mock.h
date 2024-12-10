// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_MOCK_H_
#define ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_MOCK_H_

#include <string>

#include <gmock/gmock.h>

#include "odml/cros_safety/safety_service_manager.h"

namespace cros_safety {

class SafetyServiceManagerMock : public SafetyServiceManager {
 public:
  SafetyServiceManagerMock() = default;

  MOCK_METHOD(void,
              PrepareImageSafetyClassifier,
              (base::OnceCallback<void(bool)> callback),
              (override));

  MOCK_METHOD(void,
              ClassifyImageSafety,
              (mojom::SafetyRuleset ruleset,
               const std::optional<std::string>& text,
               mojo_base::mojom::BigBufferPtr image,
               ClassifySafetyCallback callback),
              (override));

  MOCK_METHOD(void,
              ClassifyTextSafety,
              (mojom::SafetyRuleset ruleset,
               const std::string& text,
               ClassifySafetyCallback callback),
              (override));
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_MOCK_H_
