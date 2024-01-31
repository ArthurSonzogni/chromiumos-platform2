// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Reusable utilities for use in unit tests which need fakes or mocks in order
// to test out a UserDataAuth object.

#ifndef CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_
#define CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_

#include <gmock/gmock.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>

#include "cryptohome/cleanup/mock_user_oldest_activity_timestamp_manager.h"
#include "cryptohome/crypto.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_firmware_management_parameters.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

// Structure that is analogous to SystemApis, but constructed from mock objects
// for use in testing. By default all of the mock objects are constructed as
// nice mocks, but this can be overridden to specify a different value.
template <template <typename> typename MockType = ::testing::NiceMock>
struct MockSystemApis {
  UserDataAuth::BackingApis ToBackingApis() {
    return {
        .platform = &platform,
        .hwsec = &hwsec,
        .hwsec_pw_manager = &hwsec_pw_manager,
        .recovery_crypto = &recovery_crypto,
        .cryptohome_keys_manager = &cryptohome_keys_manager,
        .crypto = &crypto,
        .firmware_management_parameters = &fwmp,
        .install_attrs = &install_attrs,
        .user_activity_timestamp_manager = &user_activity_timestamp_manager,
    };
  }

  MockType<MockPlatform> platform;
  MockType<hwsec::MockCryptohomeFrontend> hwsec;
  MockType<hwsec::MockPinWeaverManagerFrontend> hwsec_pw_manager;
  MockType<hwsec::MockRecoveryCryptoFrontend> recovery_crypto;
  MockType<MockCryptohomeKeysManager> cryptohome_keys_manager;
  Crypto crypto{&hwsec, &hwsec_pw_manager, &cryptohome_keys_manager,
                &recovery_crypto};
  MockType<MockFirmwareManagementParameters> fwmp;
  MockType<MockInstallAttributes> install_attrs;
  MockType<MockUserOldestActivityTimestampManager>
      user_activity_timestamp_manager;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USERDATAAUTH_TEST_UTILS_H_
