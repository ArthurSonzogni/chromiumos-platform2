// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_
#define LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_

#include <memory>
#include <string>

#include "libec/ec_command_version_supported.h"
#include "libec/fingerprint/fp_context_command.h"

namespace ec {

class BRILLO_EXPORT FpContextCommandFactory {
 public:
  static std::unique_ptr<EcCommandInterface> Create(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported,
      const std::string& user_id);
};

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_CONTEXT_COMMAND_FACTORY_H_
