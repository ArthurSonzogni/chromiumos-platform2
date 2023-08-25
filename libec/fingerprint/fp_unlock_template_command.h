// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_UNLOCK_TEMPLATE_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_UNLOCK_TEMPLATE_COMMAND_H_

#include <algorithm>
#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpUnlockTemplateCommand
    : public EcCommand<ec_params_fp_unlock_template, EmptyParam> {
 public:
  template <typename T = FpUnlockTemplateCommand>
  static std::unique_ptr<T> Create(uint16_t finger_num) {
    static_assert(std::is_base_of<FpUnlockTemplateCommand, T>::value,
                  "Only classes derived from "
                  "FpUnlockTemplateCommand can use Create");

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    req->fgr_num = finger_num;
    return cmd;
  }
  ~FpUnlockTemplateCommand() override = default;

 protected:
  FpUnlockTemplateCommand() : EcCommand(EC_CMD_FP_UNLOCK_TEMPLATE) {}
};

static_assert(!std::is_copy_constructible<FpUnlockTemplateCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpUnlockTemplateCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_UNLOCK_TEMPLATE_COMMAND_H_
