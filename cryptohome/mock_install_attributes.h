// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_
#define CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_

#include "cryptohome/install_attributes.h"

#include <string>

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/lockbox.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

namespace cryptohome {

class MockInstallAttributes : public InstallAttributes {
 public:
  MockInstallAttributes();
  virtual ~MockInstallAttributes();

  MOCK_METHOD0(PrepareSystem, bool());

  MOCK_METHOD1(SetTpm, void(Tpm*));  // NOLINT(readability/function)

  MOCK_METHOD1(Init, bool(TpmInit*));  // NOLINT(readability/function)

  MOCK_CONST_METHOD0(IsReady, bool());

  MOCK_CONST_METHOD2(Get, bool(const std::string&, chromeos::Blob*));

  MOCK_CONST_METHOD3(GetByIndex, bool(int, std::string*, chromeos::Blob*));
  MOCK_METHOD2(Set, bool(const std::string&, const chromeos::Blob&));

  MOCK_METHOD0(Finalize, bool());

  MOCK_CONST_METHOD0(Count, int());

  MOCK_CONST_METHOD0(version, uint64_t());
  MOCK_METHOD1(set_version, void(uint64_t));

  MOCK_CONST_METHOD0(is_initialized, bool());
  MOCK_METHOD1(set_is_initialized, void(bool));  // NOLINT

  MOCK_CONST_METHOD0(is_invalid, bool());
  MOCK_METHOD1(set_is_invalid, void(bool));  // NOLINT

  MOCK_CONST_METHOD0(is_secure, bool());
  MOCK_METHOD1(set_is_secure, void(bool));  // NOLINT

  MOCK_METHOD0(lockbox_id, uint32_t());

  MOCK_METHOD0(data_path, const char*());

  MOCK_METHOD1(set_lockbox, void(Lockbox*));  // NOLINT
  MOCK_METHOD0(lockbox, Lockbox*());

  MOCK_METHOD1(set_platform, void(Platform*));  // NOLINT
  MOCK_METHOD0(platform, Platform*());

  MOCK_METHOD1(set_tpm, void(Tpm*));  // NOLINT

  MOCK_CONST_METHOD0(is_first_install, bool());
  MOCK_METHOD1(set_is_first_install, void(bool));  // NOLINT

  MOCK_CONST_METHOD1(FindIndexByName, int(const std::string&));
  MOCK_METHOD1(SerializeAttributes, bool(chromeos::Blob*));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_
