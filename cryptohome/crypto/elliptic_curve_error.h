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
class EllipticCurveErrorObj : public hwsec::error::TPMErrorBaseObj {
 public:
  inline explicit EllipticCurveErrorObj(
      const EllipticCurveErrorCode& error_code)
      : error_code_(error_code) {}
  virtual ~EllipticCurveErrorObj() = default;
  std::string ToReadableString() const;
  hwsec_foundation::error::ErrorBase SelfCopy() const;
  hwsec::error::TPMRetryAction ToTPMRetryAction() const;
  inline EllipticCurveErrorCode ErrorCode() { return error_code_; }

 protected:
  EllipticCurveErrorObj(EllipticCurveErrorObj&&) = default;

 private:
  const EllipticCurveErrorCode error_code_;
};
using EllipticCurveError = std::unique_ptr<EllipticCurveErrorObj>;

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ELLIPTIC_CURVE_ERROR_H_
