// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_NSS_UTIL_H_
#define LOGIN_MANAGER_MOCK_NSS_UTIL_H_

#include "login_manager/nss_util.h"

#include <unistd.h>

#include <base/file_path.h>
#include <base/memory/scoped_ptr.h>
#include <crypto/nss_util.h>
#include <gmock/gmock.h>

namespace crypto {
class RSAPrivateKey;
}

namespace login_manager {

class MockNssUtil : public NssUtil {
 public:
  MockNssUtil();
  virtual ~MockNssUtil();

  MOCK_METHOD0(MightHaveKeys, bool());
  MOCK_METHOD0(OpenUserDB, bool());
  MOCK_METHOD1(GetPrivateKey,
               crypto::RSAPrivateKey*(const std::vector<uint8>&));
  MOCK_METHOD0(GenerateKeyPair, crypto::RSAPrivateKey*());
  MOCK_METHOD0(GetOwnerKeyFilePath, FilePath());
  MOCK_METHOD0(GetNssdbSubpath, FilePath());
  MOCK_METHOD1(CheckPublicKeyBlob, bool(const std::vector<uint8>&));
  MOCK_METHOD8(Verify, bool(const uint8* algorithm, int algorithm_len,
                            const uint8* signature, int signature_len,
                            const uint8* data, int data_len,
                            const uint8* public_key, int public_key_len));
  MOCK_METHOD4(Sign, bool(const uint8* data, int data_len,
                          std::vector<uint8>* OUT_signature,
                          crypto::RSAPrivateKey* key));

 protected:
  crypto::ScopedTestNSSDB test_nssdb_;

  static crypto::RSAPrivateKey* CreateShortKey();

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNssUtil);
};

class CheckPublicKeyUtil : public MockNssUtil {
 public:
  CheckPublicKeyUtil(bool expected);
  virtual ~CheckPublicKeyUtil();
 private:
  DISALLOW_COPY_AND_ASSIGN(CheckPublicKeyUtil);
};

class KeyCheckUtil : public MockNssUtil {
 public:
  KeyCheckUtil();
  virtual ~KeyCheckUtil();
 private:
  DISALLOW_COPY_AND_ASSIGN(KeyCheckUtil);
};

class KeyFailUtil : public MockNssUtil {
 public:
  KeyFailUtil();
  virtual ~KeyFailUtil();
 private:
  DISALLOW_COPY_AND_ASSIGN(KeyFailUtil);
};

class SadNssUtil : public MockNssUtil {
 public:
  SadNssUtil();
  virtual ~SadNssUtil();
 private:
  DISALLOW_COPY_AND_ASSIGN(SadNssUtil);
};

class EmptyNssUtil : public MockNssUtil {
 public:
  EmptyNssUtil();
  virtual ~EmptyNssUtil();
 private:
  DISALLOW_COPY_AND_ASSIGN(EmptyNssUtil);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_NSS_UTIL_H_
