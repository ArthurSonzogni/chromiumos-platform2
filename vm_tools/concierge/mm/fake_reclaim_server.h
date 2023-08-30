// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_RECLAIM_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_RECLAIM_SERVER_H_

#include "vm_tools/concierge/mm/reclaim_server.h"
#include <unordered_map>
#include <vector>

namespace vm_tools::concierge::mm {

class FakeReclaimServer : public ReclaimServer {
 public:
  FakeReclaimServer();

  std::optional<MglruStats> GetMglruStats(const int cid) override;

  const ClientConnectionNotification& ClientConnectionCallback();

  const NewGenerationNotification& NewGenerationCallback();

  std::unordered_map<int, MglruStats> mglru_stats_{};
  std::vector<int> stats_requests_{};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_RECLAIM_SERVER_H_
