// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_CONVERTER_H_
#define CRYPTOHOME_ERROR_CONVERTER_H_

#include <vector>

#include <base/callback.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This file hosts utilities that converts the CryptohomeError class into the
// error format on the dbus.

// CryptohomeErrorToUserDataAuthError converts the CryptohomeError class into
// the error protobuf that is used by the dbus API (userdataauth).
user_data_auth::CryptohomeErrorInfo CryptohomeErrorToUserDataAuthError(
    const hwsec::StatusChain<CryptohomeError>& err,
    user_data_auth::CryptohomeErrorCode* legacy_ec);

// ReplyWithError() is a helper utility that takes the information in
// CryptohomeError and populates the relevant fields in the reply then call the
// on_done helper function.
template <typename ReplyType>
void ReplyWithError(base::OnceCallback<void(const ReplyType&)> on_done,
                    const ReplyType& reply,
                    const hwsec::StatusChain<CryptohomeError>& err);

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_CONVERTER_H_
