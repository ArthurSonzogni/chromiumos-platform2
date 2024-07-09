// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_mm_service.h"

namespace vm_tools::concierge::mm {

FakeMmService::FakeMmService(const raw_ref<MetricsLibraryInterface> metrics)
    : MmService(metrics) {}

FakeMmService::~FakeMmService() = default;

bool FakeMmService::Start() {
  return true;
}

void FakeMmService::NotifyVmStarted(apps::VmType,
                                    int,
                                    const std::string&,
                                    int64_t) {}

void FakeMmService::NotifyVmBootComplete(int) {}

void FakeMmService::NotifyVmStopping(int) {}

base::ScopedFD FakeMmService::GetKillsServerConnection() {
  return base::ScopedFD();
}

void FakeMmService::ClearBlockersUpToInclusive(int, ResizePriority) {}

void FakeMmService::ReclaimUntilBlocked(int,
                                        ResizePriority,
                                        ReclaimUntilBlockedCallback) {}

void FakeMmService::StopReclaimUntilBlocked(int) {}

}  // namespace vm_tools::concierge::mm
