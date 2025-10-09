// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_MOCK_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_MOCK_H_

#include <optional>
#include <string>

#include <gmock/gmock.h>

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils.h"

namespace hardware_verifier {

class BRILLO_EXPORT MockRuntimeHWIDUtils : public RuntimeHWIDUtils {
 public:
  MOCK_METHOD(bool, DeleteRuntimeHWIDFromDevice, (), (const, override));
  MOCK_METHOD(std::optional<std::string>,
              GetRuntimeHWID,
              (),
              (const, override));
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_MOCK_H_
