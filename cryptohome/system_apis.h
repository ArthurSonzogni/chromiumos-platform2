// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Define the standard system APIs used by the UserDataAuth service.

#ifndef CRYPTOHOME_SYSTEM_APIS_H_
#define CRYPTOHOME_SYSTEM_APIS_H_

#include <memory>

#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/pinweaver_manager/frontend.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/cryptohome_recovery_service.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_keys_manager.h"
#include "cryptohome/device_management_client_proxy.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

// Collection of APIs for accessing various aspects of the system. Used to
// populate the BackingApis parameter on non-test constructions of UserDataAuth.
struct SystemApis final {
  libstorage::Platform platform;
  hwsec::FactoryImpl hwsec_factory;
  std::unique_ptr<const hwsec::CryptohomeFrontend> hwsec{
      hwsec_factory.GetCryptohomeFrontend()};
  std::unique_ptr<const hwsec::PinWeaverManagerFrontend> hwsec_pw_manager{
      hwsec_factory.GetPinWeaverManagerFrontend()};
  std::unique_ptr<const hwsec::RecoveryCryptoFrontend> recovery_crypto{
      hwsec_factory.GetRecoveryCryptoFrontend()};
  CryptohomeKeysManager cryptohome_keys_manager{hwsec.get(), &platform};
  Crypto crypto{hwsec.get(), hwsec_pw_manager.get(), &cryptohome_keys_manager,
                recovery_crypto.get()};
  CryptohomeRecoveryAuthBlockService recovery_ab_service{&platform,
                                                         recovery_crypto.get()};
  DeviceManagementClientProxy device_management_client;
  UserOldestActivityTimestampManager user_activity_timestamp_manager{&platform};
  KeysetManagement keyset_management{&platform, &crypto,
                                     std::make_unique<VaultKeysetFactory>()};
  UssStorage uss_storage{&platform};
  UssManager uss_manager{uss_storage};
  AuthFactorManager auth_factor_manager{&platform, &keyset_management,
                                        &uss_manager};

  // Construct a backing APIs view for the UserDataAuth constructor.
  UserDataAuth::BackingApis ToBackingApis() {
    return {
        .platform = &this->platform,
        .hwsec = this->hwsec.get(),
        .hwsec_pw_manager = this->hwsec_pw_manager.get(),
        .recovery_crypto = this->recovery_crypto.get(),
        .cryptohome_keys_manager = &this->cryptohome_keys_manager,
        .crypto = &this->crypto,
        .recovery_ab_service = &this->recovery_ab_service,
        .device_management_client = &this->device_management_client,
        .user_activity_timestamp_manager =
            &this->user_activity_timestamp_manager,
        .keyset_management = &this->keyset_management,
        .uss_storage = &this->uss_storage,
        .uss_manager = &this->uss_manager,
        .auth_factor_manager = &this->auth_factor_manager,
    };
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SYSTEM_APIS_H_
