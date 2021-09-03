// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/mkbp_wake_mask_command.h"

namespace ec {

MkbpWakeMaskCommand::MkbpWakeMaskCommand(enum ec_mkbp_mask_type mask_type)
    : EcCommand(EC_CMD_MKBP_WAKE_MASK) {
  Req()->action = GET_WAKE_MASK;
  Req()->mask_type = mask_type;
}

MkbpWakeMaskCommand::MkbpWakeMaskCommand(enum ec_mkbp_mask_type mask_type,
                                         uint32_t new_wake_mask)
    : EcCommand(EC_CMD_MKBP_WAKE_MASK) {
  Req()->action = SET_WAKE_MASK;
  Req()->mask_type = mask_type;
  Req()->new_wake_mask = new_wake_mask;
}

uint32_t MkbpWakeMaskCommand::GetWakeMask() const {
  return Resp()->wake_mask;
}

MkbpWakeMaskHostEventCommand::MkbpWakeMaskHostEventCommand()
    : MkbpWakeMaskCommand(EC_MKBP_HOST_EVENT_WAKE_MASK) {}

MkbpWakeMaskHostEventCommand::MkbpWakeMaskHostEventCommand(
    uint32_t new_wake_mask)
    : MkbpWakeMaskCommand(EC_MKBP_HOST_EVENT_WAKE_MASK, new_wake_mask) {}

bool MkbpWakeMaskHostEventCommand::IsEnabled(enum host_event_code event) const {
  return EC_HOST_EVENT_MASK(event) & Resp()->wake_mask;
}

MkbpWakeMaskEventCommand::MkbpWakeMaskEventCommand()
    : MkbpWakeMaskCommand(EC_MKBP_EVENT_WAKE_MASK) {}

MkbpWakeMaskEventCommand::MkbpWakeMaskEventCommand(uint32_t new_wake_mask)
    : MkbpWakeMaskCommand(EC_MKBP_EVENT_WAKE_MASK, new_wake_mask) {}

bool MkbpWakeMaskEventCommand::IsEnabled(enum ec_mkbp_event event) const {
  // TODO(http://b/210128922): There should be a separate macro for
  //  "EC_MKBP_EVENT_MASK".
  return EC_HOST_EVENT_MASK(event) & Resp()->wake_mask;
}

}  // namespace ec
