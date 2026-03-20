// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_ROLLBACK_INFO_COMMAND_FACTORY_H_
#define LIBEC_ROLLBACK_INFO_COMMAND_FACTORY_H_

#include <memory>

#include <brillo/brillo_export.h>

#include "libec/ec_command_version_supported.h"
#include "libec/rollback_info_command.h"

namespace ec {

class BRILLO_EXPORT RollbackInfoCommandFactory {
 public:
  static std::unique_ptr<RollbackInfoCommand> Create(
      EcCommandVersionSupportedInterface* ec_cmd_ver_supported);
};

}  // namespace ec

#endif  // LIBEC_ROLLBACK_INFO_COMMAND_FACTORY_H_
