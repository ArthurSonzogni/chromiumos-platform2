// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_attestation_context.h"

#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace arc::keymint::context {

namespace {

constexpr ::keymaster::KmVersion kKeyMintVersion =
    ::keymaster::KmVersion::KEYMINT_2;

}  // namespace

class ArcAttestationContextTest : public ::testing::Test {
 protected:
  ArcAttestationContextTest() {}

  void SetUp() override {
    arc_attestation_context_ = new ArcAttestationContext(
        kKeyMintVersion, KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT);
  }

  void TearDown() override {}

  ArcAttestationContext* arc_attestation_context_;
};

}  // namespace arc::keymint::context
