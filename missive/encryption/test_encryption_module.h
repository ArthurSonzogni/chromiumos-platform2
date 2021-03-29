// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_
#define MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_

#include <base/callback.h>
#include <base/strings/string_piece.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace reporting {
namespace test {

// An |EncryptionModuleInterface| that does no encryption.
class TestEncryptionModuleStrict : public EncryptionModuleInterface {
 public:
  TestEncryptionModuleStrict();

  MOCK_METHOD(void,
              EncryptRecordImpl,
              (base::StringPiece record,
               base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb),
              (const override));

  void UpdateAsymmetricKeyImpl(
      base::StringPiece new_public_key,
      PublicKeyId new_public_key_id,
      base::OnceCallback<void(Status)> response_cb) override;

 protected:
  ~TestEncryptionModuleStrict() override;
};

// Most of the time no need to log uninterested calls to |EncryptRecord|.
typedef ::testing::NiceMock<TestEncryptionModuleStrict> TestEncryptionModule;

}  // namespace test
}  // namespace reporting

#endif  // MISSIVE_ENCRYPTION_TEST_ENCRYPTION_MODULE_H_
