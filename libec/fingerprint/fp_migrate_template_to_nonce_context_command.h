// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT_COMMAND_H_

#include <algorithm>
#include <memory>
#include <string>

#include <base/memory/ptr_util.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT FpMigrateTemplateToNonceContextCommand
    : public EcCommand<ec_params_fp_migrate_template_to_nonce_context,
                       EmptyParam> {
 public:
  static constexpr int kUserIdSize = 32;

  template <typename T = FpMigrateTemplateToNonceContextCommand>
  static std::unique_ptr<T> Create(const std::string& user_id) {
    static_assert(
        std::is_base_of<FpMigrateTemplateToNonceContextCommand, T>::value,
        "Only classes derived from "
        "FpMigrateTemplateToNonceContextCommand can use Create");
    static_assert(
        kUserIdSize ==
        sizeof(std::declval<ec_params_fp_migrate_template_to_nonce_context>()
                   .userid));
    brillo::Blob raw_user_id;
    raw_user_id.reserve(kUserIdSize);
    if (!HexStringToBytes(user_id, kUserIdSize, raw_user_id)) {
      return nullptr;
    }

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto cmd = base::WrapUnique(new T());
    auto* req = cmd->Req();
    std::copy(raw_user_id.begin(), raw_user_id.end(),
              reinterpret_cast<uint8_t*>(req->userid));
    return cmd;
  }
  ~FpMigrateTemplateToNonceContextCommand() override = default;

  static bool HexStringToBytes(const std::string& hex,
                               size_t max_size,
                               brillo::Blob& out);

 protected:
  FpMigrateTemplateToNonceContextCommand()
      : EcCommand(EC_CMD_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT) {}
};

static_assert(
    !std::is_copy_constructible<FpMigrateTemplateToNonceContextCommand>::value,
    "EcCommands are not copyable by default");
static_assert(
    !std::is_copy_assignable<FpMigrateTemplateToNonceContextCommand>::value,
    "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT_COMMAND_H_
