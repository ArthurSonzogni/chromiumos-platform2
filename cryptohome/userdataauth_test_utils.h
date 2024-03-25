// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Reusable utilities for use in unit tests which need fakes or mocks in order
// to test out a UserDataAuth object.

#ifndef CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_
#define CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_

#include <memory>

#include <base/memory/ptr_util.h>
#include <gmock/gmock.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libstorage/platform/mock_platform.h>

#include "cryptohome/auth_blocks/cryptohome_recovery_service.h"
#include "cryptohome/auth_factor/manager.h"
#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_firmware_management_parameters.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_keyset_management.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/user_secret_stash/manager.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

// Initial APIs used by all system API implementations.
struct BaseMockSystemApis {
  ::testing::NiceMock<libstorage::MockPlatform> platform{
      std::make_unique<FakePlatform>()};
  ::testing::NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  ::testing::NiceMock<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  ::testing::NiceMock<hwsec::MockRecoveryCryptoFrontend> recovery_crypto;
  ::testing::NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  Crypto crypto{&hwsec, &hwsec_pw_manager, &cryptohome_keys_manager,
                &recovery_crypto};
  ::testing::NiceMock<MockFirmwareManagementParameters> fwmp;
  CryptohomeRecoveryAuthBlockService recovery_ab_service{&platform,
                                                         &recovery_crypto};
  ::testing::NiceMock<MockInstallAttributes> install_attrs;
  ::testing::NiceMock<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager;
};

// Keyset management mock options. Tests can either select a pure mock keyset
// management object or a real keyset management object with a mock vault
// keyset factory.
struct WithMockKeysetManagement : public BaseMockSystemApis {
  ::testing::NiceMock<MockKeysetManagement> keyset_management;
};
struct WithMockVaultKeysetFactory : public BaseMockSystemApis {
  MockVaultKeysetFactory* vault_keyset_factory =
      new ::testing::NiceMock<MockVaultKeysetFactory>();
  KeysetManagement keyset_management{&this->platform, &this->crypto,
                                     base::WrapUnique(vault_keyset_factory)};
};

// Structure that is analogous to SystemApis, but constructed from mock objects
// for use in testing.
//
// The struct is a template which needs to be supplied with With* parameters
// that control some of the options of how the mock objects are constructed. For
// example, to use MockKeysetManagement to supply the KeysetManagement
// implementation you would use:
//    MockSystemApis<WithMockKeysetManagement> system_apis_;
template <typename KeysetManagementOption>
struct MockSystemApis : public KeysetManagementOption {
  UssStorage uss_storage{&this->platform};
  UssManager uss_manager{uss_storage};
  AuthFactorManager auth_factor_manager{
      &this->platform, &this->keyset_management, &this->uss_manager};

  // Construct a backing APIs view for the UserDataAuth constructor.
  UserDataAuth::BackingApis ToBackingApis() {
    return {
        .platform = &this->platform,
        .hwsec = &this->hwsec,
        .hwsec_pw_manager = &this->hwsec_pw_manager,
        .recovery_crypto = &this->recovery_crypto,
        .cryptohome_keys_manager = &this->cryptohome_keys_manager,
        .crypto = &this->crypto,
        .firmware_management_parameters = &this->fwmp,
        .recovery_ab_service = &this->recovery_ab_service,
        .install_attrs = &this->install_attrs,
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

#endif  // CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_
