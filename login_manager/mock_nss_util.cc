// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/mock_nss_util.h"

#include <pk11pub.h>
#include <secmodt.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <crypto/nss_key_util.h>
#include <crypto/nss_util.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>

namespace login_manager {
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::_;

using crypto::ScopedPK11Slot;

MockNssUtil::MockNssUtil()
    : return_bad_db_(false) {
  ON_CALL(*this, GetNssdbSubpath()).WillByDefault(Return(base::FilePath()));
}
MockNssUtil::~MockNssUtil() {}

crypto::RSAPrivateKey* MockNssUtil::CreateShortKey() {
  crypto::RSAPrivateKey* ret = nullptr;
  crypto::ScopedSECKEYPublicKey public_key_obj;
  crypto::ScopedSECKEYPrivateKey private_key_obj;
  if (crypto::GenerateRSAKeyPairNSS(test_nssdb_.slot(), 256,
                                    true /* permanent */, &public_key_obj,
                                    &private_key_obj)) {
    ret = crypto::RSAPrivateKey::CreateFromKey(private_key_obj.get());
  }
  LOG_IF(ERROR, ret == NULL) << "returning NULL!!!";
  return ret;
}

crypto::ScopedPK11Slot MockNssUtil::OpenUserDB(
    const base::FilePath& user_homedir) {
  if (return_bad_db_)
    return crypto::ScopedPK11Slot();
  return crypto::ScopedPK11Slot(PK11_ReferenceSlot(GetSlot()));
}

base::FilePath MockNssUtil::GetOwnerKeyFilePath() {
  if (!EnsureTempDir())
    return base::FilePath();
  return temp_dir_.path().AppendASCII("dummy");
}

PK11SlotInfo* MockNssUtil::GetSlot() {
  return test_nssdb_.slot();
}

bool MockNssUtil::EnsureTempDir() {
  if (!temp_dir_.IsValid() && !temp_dir_.CreateUniqueTempDir()) {
    PLOG(ERROR) << "Could not create temp dir";
    return false;
  }
  return true;
}

CheckPublicKeyUtil::CheckPublicKeyUtil(bool expected) {
  EXPECT_CALL(*this, CheckPublicKeyBlob(_)).WillOnce(Return(expected));
}

CheckPublicKeyUtil::~CheckPublicKeyUtil() {}

KeyCheckUtil::KeyCheckUtil() {
  ON_CALL(*this, GetPrivateKeyForUser(_, _))
      .WillByDefault(InvokeWithoutArgs(this, &KeyCheckUtil::CreateShortKey));
  EXPECT_CALL(*this, GetPrivateKeyForUser(_, _)).Times(1);
}

KeyCheckUtil::~KeyCheckUtil() {}

KeyFailUtil::KeyFailUtil() {
  EXPECT_CALL(*this, GetPrivateKeyForUser(_, _))
      .WillOnce(Return(reinterpret_cast<crypto::RSAPrivateKey*>(NULL)));
}

KeyFailUtil::~KeyFailUtil() {}

}  // namespace login_manager
