// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/notreached.h>

#include "cryptohome/crypto/elliptic_curve_error.h"

using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;

namespace cryptohome {

namespace {

std::string FormatEllipticCurveError(EllipticCurveErrorCode code) {
  switch (code) {
    case EllipticCurveErrorCode::kScalarOutOfRange:
      return "Elliptic curve error: Scalar out of range";
  }
  NOTREACHED() << "Unknown elliptic curve error";
  return "Unknown elliptic curve error";
}

}  // namespace

EllipticCurveError::EllipticCurveError(EllipticCurveErrorCode error_code)
    : TPMErrorBase(FormatEllipticCurveError(error_code)),
      error_code_(error_code) {}

TPMRetryAction EllipticCurveError::ToTPMRetryAction() const {
  switch (error_code_) {
    case EllipticCurveErrorCode::kScalarOutOfRange:
      return TPMRetryAction::kLater;
  }
  NOTREACHED() << "Unknown elliptic curve error";
  return TPMRetryAction::kNoRetry;
}

}  // namespace cryptohome
