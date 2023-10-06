// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_FAKE_KILLS_SERVER_H_
#define VM_TOOLS_CONCIERGE_MM_FAKE_KILLS_SERVER_H_

#include "vm_tools/concierge/mm/kills_server.h"

namespace vm_tools::concierge::mm {

class FakeKillsServer : public KillsServer {
 public:
  FakeKillsServer();

  const ClientConnectionNotification& ClientConnectionCallback();

  const ClientDisconnectedNotification& ClientDisconnectedCallback();

  const KillRequestHandler& KillRequestHandler();

  const NoKillCandidateNotification& NoKillCandidateCallback();
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_FAKE_KILLS_SERVER_H_
