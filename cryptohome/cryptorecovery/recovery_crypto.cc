// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/stl_util.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/ecdh_hkdf.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

const char RecoveryCrypto::kMediatorShareHkdfInfoValue[] = "HSM-Payload Key";

const char RecoveryCrypto::kRequestPayloadPlainTextHkdfInfoValue[] =
    "REQUEST-Payload Key";

const char RecoveryCrypto::kResponsePayloadPlainTextHkdfInfoValue[] =
    "RESPONSE-Payload Key";

const EllipticCurve::CurveType RecoveryCrypto::kCurve =
    EllipticCurve::CurveType::kPrime256;

const HkdfHash RecoveryCrypto::kHkdfHash = HkdfHash::kSha256;

const unsigned int RecoveryCrypto::kHkdfSaltLength = 32;

RecoveryCrypto::~RecoveryCrypto() = default;

}  // namespace cryptorecovery
}  // namespace cryptohome
