// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/top_sheriff.h"

#include <memory>

namespace heartd {

TopSheriff::TopSheriff() = default;

TopSheriff::~TopSheriff() = default;

void TopSheriff::StartShift() {
  for (const auto& sheriff : sheriffs) {
    sheriff->GetToWork();
  }
}

bool TopSheriff::AnyActiveSheriff() {
  for (const auto& sheriff : sheriffs) {
    if (sheriff->IsWorking()) {
      return true;
    }
  }

  return false;
}

}  // namespace heartd
