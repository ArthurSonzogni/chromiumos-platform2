// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_H_
#define ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_H_

#include <string>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"

namespace cros_safety {

// The class to manage incoming safety filter requests from other services
// (mantis, coral, etc.)
class SafetyServiceManager {
 public:
  virtual ~SafetyServiceManager() = default;

  using ClassifySafetyCallback =
      base::OnceCallback<void(mojom::SafetyClassifierVerdict)>;

  virtual void PrepareImageSafetyClassifier(
      base::OnceCallback<void(bool)> callback) = 0;

  virtual void ClassifyImageSafety(mojom::SafetyRuleset ruleset,
                                   const std::optional<std::string>& text,
                                   mojo_base::mojom::BigBufferPtr image,
                                   ClassifySafetyCallback callback) = 0;

  virtual void ClassifyTextSafety(mojom::SafetyRuleset ruleset,
                                  const std::string& text,
                                  ClassifySafetyCallback callback) = 0;
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_H_
