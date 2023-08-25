// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_
#define LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_

#include <memory>
#include <vector>

#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

// TODO(b/290989633): FpPreloadTemplateCommand is deprecated. The minimum
// interface will be kept for a while just to make separating CLs more
// convenient.
class BRILLO_EXPORT FpPreloadTemplateCommand
    : public EcCommand<EmptyParam, EmptyParam> {
 public:
  template <typename T = FpPreloadTemplateCommand>
  static std::unique_ptr<T> Create(uint16_t finger,
                                   std::vector<uint8_t> tmpl,
                                   uint16_t max_write_size) {
    return nullptr;
  }

  ~FpPreloadTemplateCommand() override = default;
};

static_assert(!std::is_copy_constructible<FpPreloadTemplateCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<FpPreloadTemplateCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_PRELOAD_TEMPLATE_COMMAND_H_
