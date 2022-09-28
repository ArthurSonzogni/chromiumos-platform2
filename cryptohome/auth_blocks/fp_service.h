// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_
#define CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_

#include <memory>
#include <string>

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

  // Start registers a given user to the fp_service.
  CryptohomeStatus Start(std::string obfuscated_username);

  // Scan initiates a fingerprint sensor session, captures a fingerprint
  // and terminates the sensor session, asynchronously through the underlying
  // fp_manager's callbacks. If a fingerprint scan result is generated,
  // a success response will be passed to the given |on_done| callback.
  // Otherwise, if the session failed to start, or the fingerprint auth
  // has been locked out, an error will be passed to the |on_done| callback.
  void Scan(base::OnceCallback<void(CryptohomeStatus)> on_done);

  // Verify if the fingerprint sensor is currently in a "successfully
  // authorized" state or not. The success or failure of this check will be
  // passed to the given |on_done| callback.
  void Verify(base::OnceCallback<void(CryptohomeStatus)> on_done);

  // Terminate stops any ongoing fingerprint sensor session and
  // clears the registered user.
  void Terminate();

 private:
  // CheckSessionStartResult forms a error status with |success|,
  //  and pass it to the |on_done| callback. This function is designed to be
  // used as a callback with FingerprintManager.
  void CheckSessionStartResult(
      base::OnceCallback<void(CryptohomeStatus)> on_done, bool success);

  // Capture captures a fingerprint through fp_manager's
  // SetAuthScanDoneCallback(). This function is designed to be
  // used by CheckSessionStartResult().
  void Capture(base::OnceCallback<void(CryptohomeStatus)> on_done);

  // Check the result of a given fingerprint scan status and then
  // forward it to the given |on_done| callback. This function is
  // designed to be used as a callback with FingerprintManager.
  void CheckFingerprintResult(
      base::OnceCallback<void(CryptohomeStatus)> on_done,
      FingerprintScanStatus status);

  // EndAuthSession terminates any ongoing fingerprint sensor session
  // and cancels all existing pending callbacks.
  void EndAuthSession();

  base::RepeatingCallback<FingerprintManager*()> fp_manager_getter_;
  // The most recent fingerprint scan result.
  FingerprintScanStatus scan_result_ =
      FingerprintScanStatus::FAILED_RETRY_NOT_ALLOWED;
  // The obfuscated username tied to the current auth session.
  std::string user_;
  // The number of attempts left in the current auth session.
  int attempts_left_ = 0;

  // TODO(b/246576446): remove the following two lines once AuthBlockUtility
  // has the functions to set up fingerprint service state.
  FRIEND_TEST(AuthBlockUtilityImplTest, VerifyFingerprintSuccess);
  FRIEND_TEST(AuthBlockUtilityImplTest, VerifyFingerprintFailure);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_FP_SERVICE_H_
