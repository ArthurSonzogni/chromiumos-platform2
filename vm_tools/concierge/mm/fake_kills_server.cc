// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_kills_server.h"

namespace vm_tools::concierge::mm {

FakeKillsServer::FakeKillsServer() : KillsServer(0) {}

const Server::ClientConnectionNotification&
FakeKillsServer::ClientConnectionCallback() {
  return Server::GetClientConnectionCallback();
}

const Server::ClientDisconnectedNotification&
FakeKillsServer::ClientDisconnectedCallback() {
  return Server::GetClientDisconnectedCallback();
}

const KillsServer::KillRequestHandler& FakeKillsServer::KillRequestHandler() {
  return KillsServer::GetKillRequestHandler();
}

const KillsServer::NoKillCandidateNotification&
FakeKillsServer::NoKillCandidateCallback() {
  return KillsServer::GetNoKillCandidateCallback();
}

}  // namespace vm_tools::concierge::mm
