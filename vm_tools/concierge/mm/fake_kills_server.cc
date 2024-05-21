// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/fake_kills_server.h"

namespace vm_tools::concierge::mm {

FakeKillsServer::FakeKillsServer() : KillsServer(0) {}

Server::ClientConnectionNotification
FakeKillsServer::ClientConnectionCallback() {
  return Server::GetClientConnectionCallbackForTesting();
}

Server::ClientDisconnectedNotification
FakeKillsServer::ClientDisconnectedCallback() {
  return Server::GetClientDisconnectedCallbackForTesting();
}

KillsServer::DecisionLatencyNotification
FakeKillsServer::DecisionLatencyCallback() {
  return KillsServer::GetDecisionLatencyCallbackForTesting();
}

KillsServer::KillRequestHandler FakeKillsServer::KillRequestHandler() {
  return KillsServer::GetKillRequestHandlerForTesting();
}

KillsServer::NoKillCandidateNotification
FakeKillsServer::NoKillCandidateCallback() {
  return KillsServer::GetNoKillCandidateCallbackForTesting();
}

}  // namespace vm_tools::concierge::mm
