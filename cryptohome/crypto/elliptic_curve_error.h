// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_ERROR_H_
#define CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_ERROR_H_

#include <memory>
#include <string>

#include <libhwsec/error/tpm_error.h>

namespace cryptohome {

// The collection of elliptic curve error code
enum class EllipticCurveErrorCode {
  kScalarOutOfRange,
};

// An EllipticCurve error.
class EllipticCurveError : public hwsec::TPMErrorBase {
 public:
  using MakeStatusTrait = hwsec::DefaultMakeStatus<EllipticCurveError>;

  explicit EllipticCurveError(EllipticCurveErrorCode error_code);
  ~EllipticCurveError() override = default;
  hwsec::TPMRetryAction ToTPMRetryAction() const override;
  EllipticCurveErrorCode ErrorCode() const { return error_code_; }

 private:
  const EllipticCurveErrorCode error_code_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_ERROR_H_
