// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/encryption/test_encryption_module.h"

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/strings/string_piece.h>
#include <base/time/default_tick_clock.h>
#include <base/time/tick_clock.h>

#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"

using ::testing::Invoke;

namespace reporting {
namespace test {

TestEncryptionModuleStrict::TestEncryptionModuleStrict()
    : EncryptionModuleInterface(/*renew_encryption_key_period=*/base::Days(1),
                                base::DefaultTickClock::GetInstance()) {
  ON_CALL(*this, EncryptRecordImpl)
      .WillByDefault(
          Invoke([](base::StringPiece record,
                    base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) {
            EncryptedRecord encrypted_record;
            encrypted_record.set_encrypted_wrapped_record(std::string(record));
            // encryption_info is not set.
            std::move(cb).Run(encrypted_record);
          }));
}

void TestEncryptionModuleStrict::UpdateAsymmetricKeyImpl(
    base::StringPiece new_public_key,
    PublicKeyId new_public_key_id,
    base::OnceCallback<void(Status)> response_cb) {
  // Ignore keys but return success.
  std::move(response_cb).Run(Status(Status::StatusOK()));
}

TestEncryptionModuleStrict::~TestEncryptionModuleStrict() = default;

}  // namespace test
}  // namespace reporting
