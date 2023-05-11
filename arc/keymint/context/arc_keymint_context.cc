// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_keymint_context.h"

namespace arc::keymint::context {

ArcKeyMintContext::ArcKeyMintContext(::keymaster::KmVersion version)
    : PureSoftKeymasterContext(version),
      rsa_key_factory_(context_adaptor_.GetWeakPtr(), KM_ALGORITHM_RSA) {}

ArcKeyMintContext::~ArcKeyMintContext() = default;

}  // namespace arc::keymint::context
