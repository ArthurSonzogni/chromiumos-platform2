// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libarc-attestation/lib/manager.h>
#include <libarc-attestation/lib/manager_base.h>

namespace arc_attestation {

ArcAttestationManagerSingleton* ArcAttestationManagerSingleton::Get() {
  if (g_instance == nullptr) {
    g_instance = new ArcAttestationManagerSingleton();
    g_instance->Setup();
  }
  return g_instance;
}

ArcAttestationManagerSingleton*
ArcAttestationManagerSingleton::CreateForTesting() {
  CHECK(!g_instance);
  g_instance = new ArcAttestationManagerSingleton();
  return g_instance;
}

void ArcAttestationManagerSingleton::DestroyForTesting() {
  if (g_instance) {
    delete g_instance;
    g_instance = nullptr;
  }
}

void ArcAttestationManagerSingleton::SetManagerForTesting(
    std::unique_ptr<ArcAttestationManagerBase> manager) {
  manager_ = std::move(manager);
}

ArcAttestationManagerBase* ArcAttestationManagerSingleton::manager() {
  return manager_.get();
}

ArcAttestationManagerSingleton::ArcAttestationManagerSingleton()
    : manager_(nullptr) {}

void ArcAttestationManagerSingleton::Setup() {
  CHECK(!manager_);
  manager_ = std::make_unique<ArcAttestationManager>();
  manager_->Setup();
}

// This instance is intentionally allowed to leak as this is a singleton in a
// library.
ArcAttestationManagerSingleton* ArcAttestationManagerSingleton::g_instance;

}  // namespace arc_attestation
