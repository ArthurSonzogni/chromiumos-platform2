// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_
#define CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_

#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/async_challenge_credential_auth_block.h"
#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/double_wrapped_compat_auth_block.h"
#include "cryptohome/auth_blocks/fingerprint_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {

// To be supported by this generic API, an AuthBlock class must implement a
// specific static API. This is the GenericAuthBlock concept.
//
// TODO(b/272098290): Make this an actual concept when C++20 is available.
// The generic auth block type must:
//   - Have a static constexpr member kType of AuthBlockType
//   - Have a static function IsSupported() that returns CryptoStatus

// Provide a collection of functions that delegates the actual operations to the
// appropriate auth block implementation, based on an AuthBlockType parameter.
//
// These operations are generally implemented by going through the list of all
// AuthBlock classes, finding one with a matching kType and then calling the
// static operation for that type. Thus for each public function there are
// usually two private functions, one handling the generic case and one handling
// the base case when no such class is found.
//
// The generic function option does not hold any internal state of its own but
// it does have pointers to all the standard "global" interfaces that the
// various AuthBlock static functions take as parameters.
class GenericAuthBlockFunctions {
 private:
  // This special template type is a way for us to "pass" a set of types to a
  // function. The actual type itself is empty and doesn't have any particular
  // value besides having a parameter pack of types attached to it.
  template <typename... Types>
  struct TypeContainer {};

  // A type container with all of the auth block types that support generic
  // functions. Usually used as the initial TypeContainer parameter for all of
  // the variadic functions.
  using AllBlockTypes = TypeContainer<PinWeaverAuthBlock,
                                      AsyncChallengeCredentialAuthBlock,
                                      DoubleWrappedCompatAuthBlock,
                                      TpmBoundToPcrAuthBlock,
                                      TpmNotBoundToPcrAuthBlock,
                                      ScryptAuthBlock,
                                      CryptohomeRecoveryAuthBlock,
                                      TpmEccAuthBlock,
                                      FingerprintAuthBlock>;

  // Generic thunk from generic to type-specific IsSupported.
  template <typename T, typename... Rest>
  CryptoStatus IsSupportedImpl(AuthBlockType auth_block_type,
                               TypeContainer<T, Rest...>) {
    if (T::kType == auth_block_type) {
      return T::IsSupported(*crypto_);
    }
    return IsSupportedImpl(auth_block_type, TypeContainer<Rest...>());
  }
  CryptoStatus IsSupportedImpl(AuthBlockType auth_block_type, TypeContainer<>) {
    return hwsec_foundation::status::MakeStatus<error::CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocGenericAuthBlockIsSupportedNotFound),
        error::ErrorActionSet({error::ErrorAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

 public:
  explicit GenericAuthBlockFunctions(Crypto* crypto) : crypto_(crypto) {}

  // Returns success if this auth block type is supported on the current
  // hardware and software environment.
  CryptoStatus IsSupported(AuthBlockType auth_block_type) {
    return IsSupportedImpl(auth_block_type, AllBlockTypes());
  }

 private:
  // Global interfaces used as parameters by the various auth block functions.
  Crypto* crypto_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_GENERIC_H_
