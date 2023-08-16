// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/kills_server.h"

namespace vm_tools::concierge::mm {

KillsServer::KillsServer(int) {}

bool KillsServer::StartListening() {
  return true;
}

}  // namespace vm_tools::concierge::mm
