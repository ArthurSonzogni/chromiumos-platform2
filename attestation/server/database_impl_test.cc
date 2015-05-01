// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/server/database_impl.h"
#include "attestation/server/mock_crypto_utility.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace {

const char kFakeCredential[] = "1234";

}  // namespace

namespace attestation {

class DatabaseImplTest : public testing::Test,
                         public DatabaseIO {
 public:
  ~DatabaseImplTest() override = default;
  void SetUp() override {
    database_.reset(new DatabaseImpl(&mock_crypto_utility_));
    database_->set_io(this);
    InitializeFakeData();
  }

  // Fake DatabaseIO::Read.
  bool Read(std::string* data) override {
    if (fake_persistent_data_readable_) {
      *data = fake_persistent_data_;
    }
    return fake_persistent_data_readable_;
  }

  // Fake DatabaseIO::Write.
  bool Write(const std::string& data) override {
    if (fake_persistent_data_writable_) {
      fake_persistent_data_ = data;
    }
    return fake_persistent_data_writable_;
  }

  // Fake DatabaseIO::Watch.
  void Watch(const base::Closure& callback) override {
    fake_watch_callback_ = callback;
  }

  // Initializes fake_persistent_data_ with a default value.
  void InitializeFakeData() {
    AttestationDatabase proto;
    proto.mutable_credentials()->set_conformance_credential(kFakeCredential);
    proto.SerializeToString(&fake_persistent_data_);
  }

 protected:
  std::string fake_persistent_data_;
  bool fake_persistent_data_readable_{true};
  bool fake_persistent_data_writable_{true};
  base::Closure fake_watch_callback_;
  NiceMock<MockCryptoUtility> mock_crypto_utility_;
  std::unique_ptr<DatabaseImpl> database_;
};

TEST_F(DatabaseImplTest, ReadSuccess) {
  EXPECT_TRUE(database_->Initialize());
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().conformance_credential());
}

TEST_F(DatabaseImplTest, ReadFailure) {
  fake_persistent_data_readable_ = false;
  EXPECT_FALSE(database_->Initialize());
  EXPECT_FALSE(database_->GetProtobuf().has_credentials());
}

TEST_F(DatabaseImplTest, DecryptFailure) {
  EXPECT_CALL(mock_crypto_utility_, DecryptData(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(database_->Initialize());
  EXPECT_FALSE(database_->GetProtobuf().has_credentials());
}

TEST_F(DatabaseImplTest, WriteSuccess) {
  EXPECT_TRUE(database_->Initialize());
  database_->GetMutableProtobuf()->mutable_credentials()->
      set_platform_credential("test");
  std::string expected_data;
  database_->GetProtobuf().SerializeToString(&expected_data);
  EXPECT_TRUE(database_->SaveChanges());
  EXPECT_EQ(expected_data, fake_persistent_data_);
}

TEST_F(DatabaseImplTest, WriteFailure) {
  fake_persistent_data_writable_ = false;
  EXPECT_TRUE(database_->Initialize());
  database_->GetMutableProtobuf()->mutable_credentials()->
      set_platform_credential("test");
  EXPECT_FALSE(database_->SaveChanges());
}

TEST_F(DatabaseImplTest, EncryptFailure) {
  EXPECT_CALL(mock_crypto_utility_, EncryptData(_, _, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(database_->Initialize());
  database_->GetMutableProtobuf()->mutable_credentials()->
      set_platform_credential("test");
  EXPECT_FALSE(database_->SaveChanges());
}

TEST_F(DatabaseImplTest, IgnoreLegacyEncryptJunk) {
  // Legacy encryption scheme appended a SHA-1 hash before encrypting.
  fake_persistent_data_ += std::string(20, 'A');
  EXPECT_TRUE(database_->Initialize());
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().conformance_credential());
}

TEST_F(DatabaseImplTest, Reload) {
  EXPECT_TRUE(database_->Initialize());
  AttestationDatabase proto;
  proto.mutable_credentials()->set_platform_credential(kFakeCredential);
  proto.SerializeToString(&fake_persistent_data_);
  EXPECT_EQ(std::string(),
            database_->GetProtobuf().credentials().platform_credential());
  EXPECT_TRUE(database_->Reload());
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().platform_credential());
}

TEST_F(DatabaseImplTest, AutoReload) {
  EXPECT_TRUE(database_->Initialize());
  AttestationDatabase proto;
  proto.mutable_credentials()->set_platform_credential(kFakeCredential);
  proto.SerializeToString(&fake_persistent_data_);
  EXPECT_EQ(std::string(),
            database_->GetProtobuf().credentials().platform_credential());
  fake_watch_callback_.Run();
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().platform_credential());
}

}  // namespace attestation
