// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_
#define LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_

#include <memory>
#include <string>

#include "biod/cros_fp_device_interface.h"
#include "libec/fingerprint/fp_context_command.h"

namespace ec {

class BRILLO_EXPORT FpContextCommandFactory {
 public:
  static std::unique_ptr<EcCommandInterface> Create(
      biod::CrosFpDeviceInterface* cros_fp, const std::string& user_id);
};

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_
