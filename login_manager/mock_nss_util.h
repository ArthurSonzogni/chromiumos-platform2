// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_NSS_UTIL_H_
#define LOGIN_MANAGER_MOCK_NSS_UTIL_H_

#include "login_manager/nss_util.h"

#include <stdint.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <crypto/nss_util.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>
#include <crypto/scoped_test_nss_db.h>
#include <gmock/gmock.h>

namespace crypto {
class RSAPrivateKey;
}

namespace login_manager {
// Forward declaration.
typedef struct PK11SlotInfoStr PK11SlotInfo;

class MockNssUtil : public NssUtil {
 public:
  MockNssUtil();
  MockNssUtil(const MockNssUtil&) = delete;
  MockNssUtil& operator=(const MockNssUtil&) = delete;

  ~MockNssUtil() override;

  std::unique_ptr<crypto::RSAPrivateKey> CreateShortKey();

  MOCK_METHOD(bool,
              CheckPublicKeyBlob,
              (const std::vector<uint8_t>&),
              (override));
  MOCK_METHOD(bool,
              Verify,
              (const std::vector<uint8_t>&,
               const std::vector<uint8_t>&,
               const std::vector<uint8_t>&,
               const crypto::SignatureVerifier::SignatureAlgorithm),
              (override));
  base::FilePath GetOwnerKeyFilePath() override;

  PK11SlotDescriptor* GetDescriptor();
  PK11SlotInfo* GetSlot();

  // Ensures that temp_dir_ is created and accessible.
  bool EnsureTempDir();

 protected:
  bool return_bad_db_ = false;
  crypto::ScopedTestNSSDB test_nssdb_;
  base::ScopedTempDir temp_dir_;
  ScopedPK11SlotDescriptor desc_;
};

class CheckPublicKeyUtil : public MockNssUtil {
 public:
  explicit CheckPublicKeyUtil(bool expected);
  CheckPublicKeyUtil(const CheckPublicKeyUtil&) = delete;
  CheckPublicKeyUtil& operator=(const CheckPublicKeyUtil&) = delete;

  ~CheckPublicKeyUtil() override;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_NSS_UTIL_H_
