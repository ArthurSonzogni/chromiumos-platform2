// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/mm_service.h"

namespace vm_tools::concierge::mm {

bool MmService::Start() {
  return true;
}

void MmService::NotifyVmStarted(apps::VmType, int, const std::string&) {
  return;
}

void MmService::NotifyVmStopping(int) {
  return;
}

base::ScopedFD MmService::GetKillsServerConnection(uint32_t) {
  return {};
}

}  // namespace vm_tools::concierge::mm
