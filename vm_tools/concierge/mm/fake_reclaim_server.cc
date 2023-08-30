// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_reclaim_server.h"

#include <memory>

namespace vm_tools::concierge::mm {

FakeReclaimServer::FakeReclaimServer() : ReclaimServer(0) {}

std::optional<MglruStats> FakeReclaimServer::GetMglruStats(const int cid) {
  stats_requests_.emplace_back(cid);

  if (mglru_stats_.find(cid) == mglru_stats_.end()) {
    return std::nullopt;
  }

  return mglru_stats_[cid];
}

const Server::ClientConnectionNotification&
FakeReclaimServer::ClientConnectionCallback() {
  return Server::GetClientConnectionCallback();
}

const ReclaimServer::NewGenerationNotification&
FakeReclaimServer::NewGenerationCallback() {
  return ReclaimServer::GetNewGenerationCallback();
}

}  // namespace vm_tools::concierge::mm
