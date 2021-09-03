// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_MKBP_WAKE_MASK_COMMAND_H_
#define LIBEC_MKBP_WAKE_MASK_COMMAND_H_

#include <brillo/brillo_export.h>
#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT MkbpWakeMaskCommand
    : public EcCommand<struct ec_params_mkbp_event_wake_mask,
                       struct ec_response_mkbp_event_wake_mask> {
 public:
  explicit MkbpWakeMaskCommand(enum ec_mkbp_mask_type mask_type);
  MkbpWakeMaskCommand(enum ec_mkbp_mask_type mask_type, uint32_t new_wake_mask);
  ~MkbpWakeMaskCommand() override = default;

  uint32_t GetWakeMask() const;
};

class BRILLO_EXPORT MkbpWakeMaskHostEventCommand : public MkbpWakeMaskCommand {
 public:
  MkbpWakeMaskHostEventCommand();
  explicit MkbpWakeMaskHostEventCommand(uint32_t new_wake_mask);
  ~MkbpWakeMaskHostEventCommand() override = default;

  bool IsEnabled(enum host_event_code event) const;
};

class BRILLO_EXPORT MkbpWakeMaskEventCommand : public MkbpWakeMaskCommand {
 public:
  MkbpWakeMaskEventCommand();
  explicit MkbpWakeMaskEventCommand(uint32_t new_wake_mask);
  ~MkbpWakeMaskEventCommand() override = default;

  bool IsEnabled(enum ec_mkbp_event event) const;
};

static_assert(!std::is_copy_constructible<MkbpWakeMaskCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<MkbpWakeMaskCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_MKBP_WAKE_MASK_COMMAND_H_
