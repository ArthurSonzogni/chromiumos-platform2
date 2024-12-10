// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_BYPASS_H_
#define ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_BYPASS_H_

#include <string>
#include <utility>

#include "odml/cros_safety/safety_service_manager.h"

namespace cros_safety {

// Fake SafetyService that always return kPass for incoming requests.
class SafetyServiceManagerBypass : public SafetyServiceManager {
 public:
  SafetyServiceManagerBypass() = default;

  void PrepareImageSafetyClassifier(
      base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(true);
  };

  void ClassifyImageSafety(mojom::SafetyRuleset ruleset,
                           const std::optional<std::string>& text,
                           mojo_base::mojom::BigBufferPtr image,
                           ClassifySafetyCallback callback) override {
    LOG(INFO) << "Fake ClassifyImageSafety was called; return kPass directly";
    std::move(callback).Run(mojom::SafetyClassifierVerdict::kPass);
  }

  void ClassifyTextSafety(mojom::SafetyRuleset ruleset,
                          const std::string& text,
                          ClassifySafetyCallback callback) override {
    LOG(INFO) << "Fake ClassifyTextSafety was called; return kPass directy";
    std::move(callback).Run(mojom::SafetyClassifierVerdict::kPass);
  }
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_BYPASS_H_
