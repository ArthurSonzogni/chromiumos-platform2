// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include "hermes/euicc_event.h"

namespace hermes {

EuiccEvent::EuiccEvent(uint32_t slot, EuiccStep step, EuiccOp op)
    : slot(slot), step(step), op(op) {}

EuiccEvent::EuiccEvent(uint32_t slot, EuiccStep step)
    : slot(slot), step(step), op(EuiccOp::UNKNOWN) {}

std::ostream& operator<<(std::ostream& os, const EuiccStep& rhs) {
  switch (rhs) {
    case (EuiccStep::START):
      os << "START";
      break;
    case (EuiccStep::PENDING_NOTIFICATIONS):
      os << "PENDING_NOTIFICATIONS";
      break;
    case (EuiccStep::END):
      os << "END";
      break;
    default:
      os << rhs;
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const EuiccOp& rhs) {
  switch (rhs) {
    case (EuiccOp::UNKNOWN):
      os << "UNKNOWN";
      break;
    case (EuiccOp::ENABLE):
      os << "ENABLE";
      break;
    case (EuiccOp::DISABLE):
      os << "DISABLE";
      break;
    default:
      os << rhs;
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const EuiccEvent& rhs) {
  os << "Slot: " << rhs.slot << ", Step:" << rhs.step << ", Op:" << rhs.op;
  return os;
}

}  // namespace hermes
