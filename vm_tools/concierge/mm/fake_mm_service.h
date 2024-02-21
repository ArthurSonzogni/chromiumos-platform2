// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_MM_SERVICE_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_MM_SERVICE_H_

#include <memory>
#include <string>

#include "vm_tools/concierge/mm/mm_service.h"

namespace vm_tools::concierge::mm {

class FakeMmService : public MmService {
 public:
  explicit FakeMmService(const raw_ref<MetricsLibraryInterface> metrics);

  ~FakeMmService() override;

  bool Start() override;

  void NotifyVmStarted(apps::VmType vm_type,
                       int vm_cid,
                       const std::string& socket) override;

  void NotifyVmBootComplete(int vm_cid) override;

  void NotifyVmStopping(int vm_cid) override;

  void ReclaimUntilBlocked(int vm_cid,
                           ResizePriority priority,
                           ReclaimUntilBlockedCallback cb) override;

  void StopReclaimUntilBlocked(int vm_cid) override;

  base::ScopedFD GetKillsServerConnection() override;

  void ClearBlockersUpToInclusive(int vm_cid, ResizePriority priority) override;
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_MM_SERVICE_H_
