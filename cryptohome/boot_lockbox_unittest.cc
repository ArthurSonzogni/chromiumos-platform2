// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/boot_lockbox.h"

#include <map>
#include <string>

#include <base/stl_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using testing::NiceMock;
using testing::WithArgs;
using testing::Return;

namespace cryptohome {

// The DER encoding of SHA-256 DigestInfo as defined in PKCS #1.
const unsigned char kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
    0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

class BootLockboxTest : public testing::Test {
 public:
  BootLockboxTest() : is_fake_extended_(false), rsa_(NULL) {}
  virtual ~BootLockboxTest() {
    if (rsa_)
      RSA_free(rsa_);
  }

  void SetUp() {
    // Configure a fake TPM.
    ON_CALL(tpm_, Sign(_, _, _))
        .WillByDefault(WithArgs<1, 2>(Invoke(this,
                                             &BootLockboxTest::FakeSign)));
    ON_CALL(tpm_, CreatePCRBoundKey(_, _, _, _))
        .WillByDefault(WithArgs<3>(Invoke(this, &BootLockboxTest::FakeCreate)));
    ON_CALL(tpm_, VerifyPCRBoundKey(_, _, _))
        .WillByDefault(Return(true));
    ON_CALL(tpm_, ExtendPCR(_, _))
        .WillByDefault(InvokeWithoutArgs(this, &BootLockboxTest::FakeExtend));
    ON_CALL(tpm_, ReadPCR(_, _))
        .WillByDefault(WithArgs<1>(Invoke(this,
                                          &BootLockboxTest::FakeReadPCR)));
    ON_CALL(crypto_, EncryptWithTpm(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeEncrypt));
    ON_CALL(crypto_, DecryptWithTpm(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeDecrypt));
    // Configure a fake filesystem.
    ON_CALL(platform_, WriteStringToFileAtomicDurable(_, _, _))
        .WillByDefault(WithArgs<0, 1>(Invoke(this,
                                             &BootLockboxTest::FakeWriteFile)));
    ON_CALL(platform_, ReadFileToString(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeReadFile));
    lockbox_.reset(new BootLockbox(&tpm_, &platform_, &crypto_));
    lockbox2_.reset(new BootLockbox(&tpm_, &platform_, &crypto_));
  }

  bool FakeSign(const chromeos::SecureBlob& input,
                chromeos::SecureBlob* signature) {
    if (is_fake_extended_)
      return false;
    chromeos::SecureBlob der_header(std::begin(kSha256DigestInfo),
                                    std::end(kSha256DigestInfo));
    chromeos::SecureBlob der_encoded_input = chromeos::SecureBlob::Combine(
        der_header,
        CryptoLib::Sha256(input));
    unsigned char buffer[256];
    int length = RSA_private_encrypt(
          der_encoded_input.size(),
          der_encoded_input.data(),
          buffer, rsa(), RSA_PKCS1_PADDING);
    chromeos::SecureBlob tmp(buffer, buffer + length);
    signature->swap(tmp);
    return true;
  }

  bool FakeCreate(chromeos::SecureBlob* public_key) {
    if (is_fake_extended_)
      return false;
    if (rsa_) {
      RSA_free(rsa_);
      rsa_ = NULL;
    }
    unsigned char* buffer = NULL;
    int length = i2d_RSAPublicKey(rsa(), &buffer);
    if (length <= 0)
      return false;
    chromeos::SecureBlob tmp(buffer, buffer + length);
    public_key->swap(tmp);
    OPENSSL_free(buffer);
    return true;
  }

  bool FakeExtend() {
    is_fake_extended_ = true;
    return true;
  }

  bool FakeReadPCR(chromeos::SecureBlob* pcr) {
    pcr->assign(20, is_fake_extended_ ? 0xAA : 0);
    return true;
  }

  bool FakeWriteFile(const std::string& path, const std::string& data) {
    fake_files_[path] = data;
    return true;
  }

  bool FakeReadFile(const std::string& path, std::string* data) {
    if (fake_files_.count(path) == 0)
      return false;
    *data = fake_files_[path];
    return true;
  }

  bool FakeEncrypt(const chromeos::SecureBlob& in, std::string* out) {
    *out = in.to_string();
    return true;
  }

  bool FakeDecrypt(const std::string& in, chromeos::SecureBlob* out) {
    *out = chromeos::SecureBlob(in);
    return true;
  }

 protected:
  NiceMock<MockTpm> tpm_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCrypto> crypto_;
  scoped_ptr<BootLockbox> lockbox_;
  scoped_ptr<BootLockbox> lockbox2_;
  bool is_fake_extended_;
  std::map<std::string, std::string> fake_files_;
  RSA* rsa_;  // Access with rsa().

  RSA* rsa() {
    if (!rsa_) {
      rsa_ = RSA_generate_key(2048, 65537, NULL, NULL);
      CHECK(rsa_);
    }
    return rsa_;
  }
};

TEST_F(BootLockboxTest, NormalUse) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  EXPECT_LT(0, signature.size());
  ASSERT_TRUE(lockbox_->Verify(data, signature));
  EXPECT_TRUE(lockbox_->FinalizeBoot());
  ASSERT_TRUE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, SignAfterFinalize) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, CreateAfterFinalize) {
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, VerifyIsFinalized) {
  ASSERT_FALSE(lockbox_->IsFinalized());
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  ASSERT_TRUE(lockbox_->IsFinalized());
}

TEST_F(BootLockboxTest, LoadFromFile) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  // Verify in another instance which needs to load the key.
  ASSERT_TRUE(lockbox2_->Verify(data, signature));
}

TEST_F(BootLockboxTest, FileErrors) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));

  EXPECT_CALL(platform_, WriteStringToFileAtomicDurable(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, ReadFileToString(_, _)).WillRepeatedly(Return(false));

  EXPECT_FALSE(lockbox2_->Sign(data, &signature));
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  EXPECT_TRUE(lockbox2_->FinalizeBoot());
}

TEST_F(BootLockboxTest, SignError) {
  EXPECT_CALL(tpm_, Sign(_, _, _)).WillRepeatedly(Return(false));

  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, ExtendPCRError) {
  EXPECT_CALL(tpm_, ExtendPCR(_, _)).WillRepeatedly(Return(false));
  ASSERT_FALSE(lockbox_->FinalizeBoot());
}

TEST_F(BootLockboxTest, VerifyWithBadKey) {
  EXPECT_CALL(tpm_, VerifyPCRBoundKey(_, _, _)).WillRepeatedly(Return(false));
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_FALSE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, VerifyWithNoKey) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, VerifyWithBadSignature) {
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_TRUE(lockbox_->Verify(data, signature));
  signature.swap(data);
  ASSERT_FALSE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, EncryptError) {
  // Induce encryption failures; we expect a key cannot be successfully created.
  EXPECT_CALL(crypto_, EncryptWithTpm(_, _)).WillRepeatedly(Return(false));
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, DecryptError) {
  // Induce decryption failures; we expect keys can be created and written to
  // 'disk' but they cannot be loaded again.
  EXPECT_CALL(crypto_, DecryptWithTpm(_, _)).WillRepeatedly(Return(false));
  chromeos::SecureBlob data(100);
  chromeos::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  EXPECT_TRUE(lockbox_->Verify(data, signature));
  // A second instance will not be able to load from disk.
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  chromeos::SecureBlob signature2;
  // Sign() should still succeed because it can create a new key.
  EXPECT_TRUE(lockbox2_->Sign(data, &signature2));
  EXPECT_TRUE(lockbox2_->Verify(data, signature2));
  // Now the two instances should have different keys.
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  EXPECT_FALSE(lockbox_->Verify(data, signature2));
}

}  // namespace cryptohome
