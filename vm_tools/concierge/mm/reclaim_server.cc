// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/reclaim_server.h"

namespace vm_tools::concierge::mm {

ReclaimServer::ReclaimServer(int) {}

bool ReclaimServer::StartListening() {
  return true;
}

}  // namespace vm_tools::concierge::mm
