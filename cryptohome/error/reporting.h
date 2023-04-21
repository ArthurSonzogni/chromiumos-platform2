// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_REPORTING_H_
#define CRYPTOHOME_ERROR_REPORTING_H_

#include <string>

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
    const user_data_auth::CryptohomeErrorInfo& info,
    const std::string& error_bucket_name);

// Report a Cryptohome Ok status. For each error bucket, if the error bucket
// represents the error results of a logical operation (like a dbus request),
// where each operation reports exactly 1 error to the bucket when failing, then
// when the operation succeeds, it can report an Ok status using this function.
// This can make the error bucket show meaningful results of error/success
// percentage for each operation.
void ReportCryptohomeOk(const std::string& error_bucket_name);

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_REPORTING_H_
