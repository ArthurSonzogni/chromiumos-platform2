// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_
#define CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_

#include <memory>

#include <base/callback.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fingerprint_manager.h"

namespace cryptohome {

class FingerprintAuthBlockService {
 public:
  explicit FingerprintAuthBlockService(
      base::RepeatingCallback<FingerprintManager*()> fp_manager_getter);

  FingerprintAuthBlockService(const FingerprintAuthBlockService&) = delete;
  FingerprintAuthBlockService& operator=(const FingerprintAuthBlockService&) =
      delete;

  ~FingerprintAuthBlockService() = default;

  // Create a null instance of this service that will not have any of the
  // underlying services available and so will not be able to do anything.
  //
  // This is mostly useful in tests where you need a copy of the service but
  // don't actually need any fingerprint operations to work.
  static std::unique_ptr<FingerprintAuthBlockService> MakeNullService();

  // Verify if the fingerprint sensor is currently in a "successfully
  // authorized" state or not. The success or failure of this check will be
  // passed to the given |on_done| callback.
  void Verify(base::OnceCallback<void(CryptohomeStatus)> on_done);

 private:
  base::RepeatingCallback<FingerprintManager*()> fp_manager_getter_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_
