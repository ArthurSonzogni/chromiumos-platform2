// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_
#define MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_

#include <string_view>

#include <base/functional/callback.h>
#include <gmock/gmock.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"

namespace reporting::test {

// An |EncryptionModuleInterface| that does no encryption.
class TestEncryptionModuleStrict : public EncryptionModuleInterface {
 public:
  explicit TestEncryptionModuleStrict(bool is_enabled);

  MOCK_METHOD(void,
              EncryptRecordImpl,
              (std::string_view record,
               base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb),
              (const override));

  void UpdateAsymmetricKeyImpl(
      std::string_view new_public_key,
      PublicKeyId new_public_key_id,
      base::OnceCallback<void(Status)> response_cb) override;

 protected:
  ~TestEncryptionModuleStrict() override;
};

// Most of the time no need to log uninterested calls to |EncryptRecord|.
typedef ::testing::NiceMock<TestEncryptionModuleStrict> TestEncryptionModule;

}  // namespace reporting::test

#endif  // MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_
