// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/mock_nss_util.h"

#include <pk11pub.h>
#include <secmodt.h>
#include <unistd.h>

#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <crypto/nss_key_util.h>
#include <crypto/nss_util.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>

namespace login_manager {
using ::testing::_;
using ::testing::ByMove;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;

using crypto::ScopedPK11Slot;

MockNssUtil::MockNssUtil() {
  desc_ = std::make_unique<PK11SlotDescriptor>();
  desc_->slot = ScopedPK11Slot(PK11_ReferenceSlot(GetSlot()));
  desc_->ns_mnt_path = std::nullopt;
}

MockNssUtil::~MockNssUtil() = default;

std::unique_ptr<crypto::RSAPrivateKey> MockNssUtil::CreateShortKey() {
  std::unique_ptr<crypto::RSAPrivateKey> ret;
  crypto::ScopedSECKEYPublicKey public_key_obj;
  crypto::ScopedSECKEYPrivateKey private_key_obj;
  if (crypto::GenerateRSAKeyPairNSS(test_nssdb_.slot(), 256,
                                    true /* permanent */, &public_key_obj,
                                    &private_key_obj)) {
    ret.reset(crypto::RSAPrivateKey::CreateFromKey(private_key_obj.get()));
  }
  LOG_IF(ERROR, ret == nullptr) << "returning nullptr!!!";
  return ret;
}

base::FilePath MockNssUtil::GetOwnerKeyFilePath() {
  if (!EnsureTempDir()) {
    return base::FilePath();
  }
  return temp_dir_.GetPath().AppendASCII("fake");
}

PK11SlotDescriptor* MockNssUtil::GetDescriptor() {
  return desc_.get();
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

CheckPublicKeyUtil::~CheckPublicKeyUtil() = default;

}  // namespace login_manager
