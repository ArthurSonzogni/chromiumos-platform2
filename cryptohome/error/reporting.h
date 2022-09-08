// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_REPORTING_H_
#define CRYPTOHOME_ERROR_REPORTING_H_

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// Report an instance of CryptohomeError status chain to UMA, it'll
// automatically disect the status chain and figure out which UMAs need to be
// reported. It is expected that the caller have already called
// CryptohomeErrorToUserDataAuthError before calling this, and |info| is the
// result from it. If |info| doesn't match |err|, the behaviour is undefined.
void ReportCryptohomeError(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& err,
    const user_data_auth::CryptohomeErrorInfo& info);

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_REPORTING_H_
