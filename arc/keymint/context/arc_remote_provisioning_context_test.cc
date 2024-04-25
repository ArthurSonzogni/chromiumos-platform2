// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <memory>
#include <utility>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <libarc-attestation/lib/test_utils.h>

namespace arc::keymint::context {

namespace {
using testing::NiceMock;
}

class ArcRemoteProvisioningContextTest : public ::testing::Test {
 protected:
  ArcRemoteProvisioningContextTest() {}

  void SetUp() override {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
    arc_attestation::ArcAttestationManagerSingleton::CreateForTesting();
    std::unique_ptr<NiceMock<arc_attestation::MockArcAttestationManager>>
        manager = std::make_unique<
            NiceMock<arc_attestation::MockArcAttestationManager>>();
    manager_ = manager.get();
    arc_attestation::ArcAttestationManagerSingleton::Get()
        ->SetManagerForTesting(std::move(manager));
  }

  void TearDown() override {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
  }

  NiceMock<arc_attestation::MockArcAttestationManager>* manager_;
  ArcRemoteProvisioningContext* context_;
};

}  // namespace arc::keymint::context
