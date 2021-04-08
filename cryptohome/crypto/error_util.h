// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_ERROR_UTIL_H_
#define CRYPTOHOME_CRYPTO_ERROR_UTIL_H_

#include <string>

namespace cryptohome {

// Returns all errors in OpenSSL error queue delimited with a semicolon
// starting from the earliest. Returns empty string if there are no errors in
// the queue. Clears the queue.
std::string GetOpenSSLErrors();

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_ERROR_UTIL_H_
