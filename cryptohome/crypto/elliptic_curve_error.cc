// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/notreached.h>

#include "cryptohome/crypto/elliptic_curve_error.h"

using hwsec::error::TPMRetryAction;

namespace cryptohome {

std::string EllipticCurveErrorObj::ToReadableString() const {
  switch (error_code_) {
    case EllipticCurveErrorCode::kScalarOutOfRange:
      return "Elliptic curve error: Scalar out of range";
  }
  NOTREACHED() << "Unknown elliptic curve error";
  return "Unknown elliptic curve error";
}

hwsec_foundation::error::ErrorBase EllipticCurveErrorObj::SelfCopy() const {
  return std::make_unique<EllipticCurveErrorObj>(error_code_);
}

TPMRetryAction EllipticCurveErrorObj::ToTPMRetryAction() const {
  switch (error_code_) {
    case EllipticCurveErrorCode::kScalarOutOfRange:
      return TPMRetryAction::kLater;
  }
  NOTREACHED() << "Unknown elliptic curve error";
  return TPMRetryAction::kNoRetry;
}

}  // namespace cryptohome
