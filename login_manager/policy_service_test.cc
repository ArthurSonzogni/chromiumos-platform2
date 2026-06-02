// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_service.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/threading/thread.h>
#include <brillo/errors/error.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bindings/device_management_backend.pb.h"
#include "crypto/signature_verifier.h"
#include "login_manager/blob_util.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_policy_store.h"

namespace em = enterprise_management;

using ::testing::_;
using ::testing::DoAll;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Sequence;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

namespace {

constexpr char kPolicyValue1[] = "fake_policy1";
constexpr char kPolicyValue2[] = "fake_policy2";
constexpr char kPolicyValue3[] = "fake_policy3";

}  // namespace

namespace login_manager {

class PolicyServiceTest : public testing::Test {
 public:
  PolicyServiceTest() = default;

  void SetUp() override {
    fake_loop_.SetAsCurrent();
    store_ = new StrictMock<MockPolicyStore>;
    service_ = std::make_unique<PolicyService>(
        &system_utils_, /*policy_dir=*/base::FilePath(), /*policy_key=*/&key_,
        /*extension_install_policy_key=*/&extension_install_key_,
        /*metrics=*/nullptr,
        /*resilient_chrome_policy_store=*/false);
    service_->SetStoreForTesting(MakeChromePolicyNamespace(),
                                 std::unique_ptr<PolicyStore>(store_));
    service_->set_delegate(&delegate_);
  }

  void InitPolicy(const std::vector<uint8_t>& data,
                  const std::vector<uint8_t>& signature,
                  const std::vector<uint8_t>& key,
                  const std::vector<uint8_t>& key_signature) {
    policy_proto_.Clear();
    if (!data.empty()) {
      policy_proto_.set_policy_data(BlobToString(data));
    }
    if (!signature.empty()) {
      policy_proto_.set_policy_data_signature(BlobToString(signature));
    }
    if (!key.empty()) {
      policy_proto_.set_new_public_key(BlobToString(key));
    }
    if (!key_signature.empty()) {
      policy_proto_.set_new_public_key_signature(BlobToString(key_signature));
    }
  }

  void ExpectVerifyAndSetPolicy(Sequence* sequence) {
    EXPECT_CALL(key(), Verify(fake_data_, fake_sig_, _))
        .InSequence(*sequence)
        .WillOnce(Return(true));
    EXPECT_CALL(store(), Set(ProtoEq(policy_proto_)))
        .Times(1)
        .InSequence(*sequence);
  }

  void ExpectSetPolicy(Sequence* sequence) {
    EXPECT_CALL(store(), Set(ProtoEq(policy_proto_)))
        .Times(1)
        .InSequence(*sequence);
  }

  void ExpectPersistKey(MockPolicyKey& key, Sequence* sequence) {
    EXPECT_CALL(key, Persist()).InSequence(*sequence).WillOnce(Return(true));
    EXPECT_CALL(delegate_, OnKeyPersisted(&key, true));
  }

  void ExpectPersistPolicy(Sequence* sequence) {
    EXPECT_CALL(store(), Persist())
        .InSequence(*sequence)
        .WillOnce(Return(true));
    EXPECT_CALL(delegate_, OnPolicyPersisted(true));
  }

  void ExpectKeyEquals(MockPolicyKey& key, bool result, Sequence* sequence) {
    EXPECT_CALL(key, Equals(_))
        .InSequence(*sequence)
        .WillRepeatedly(Return(result));
  }

  void ExpectKeyPopulated(MockPolicyKey& key,
                          bool return_value,
                          Sequence* sequence) {
    EXPECT_CALL(key, IsPopulated())
        .InSequence(*sequence)
        .WillRepeatedly(Return(return_value));
  }

  void ExpectStoreFail(int flags, const std::string& code) {
    EXPECT_CALL(key_, Persist()).Times(0);
    EXPECT_CALL(*store_, Set(_)).Times(0);
    EXPECT_CALL(*store_, Persist()).Times(0);

    std::optional<std::string> actual_error_code;
    service_->Store(MakeChromePolicyNamespace(), SerializeAsBlob(policy_proto_),
                    flags,
                    base::BindLambdaForTesting(
                        [&actual_error_code](brillo::ErrorPtr error) {
                          actual_error_code = error->GetCode();
                          return;
                        }));
    fake_loop_.Run();

    ASSERT_TRUE(actual_error_code.has_value());
    EXPECT_EQ(*actual_error_code, code);
  }

  static const int kAllKeyFlags;
  static const char kSignalSuccess[];
  static const char kSignalFailure[];

  const std::vector<uint8_t> fake_data_ = StringToBlob("fake_data");
  const std::vector<uint8_t> fake_sig_ = StringToBlob("fake_signature");
  const std::vector<uint8_t> fake_key_ = StringToBlob("fake_key");
  const std::vector<uint8_t> fake_key_sig_ = StringToBlob("fake_key_signature");

  const std::vector<uint8_t> empty_blob_;

  // Various representations of the policy protobuf.
  em::PolicyFetchResponse policy_proto_;

  brillo::FakeMessageLoop fake_loop_{nullptr};
  FakeSystemUtils system_utils_;

  // Use StrictMock to make sure that no unexpected policy or key mutations can
  // occur without the test failing.
  StrictMock<MockPolicyKey> key_;
  StrictMock<MockPolicyKey> extension_install_key_;
  StrictMock<MockPolicyStore>* store_;
  MockPolicyServiceDelegate delegate_;

  virtual MockPolicyKey& key() { return key_; }
  virtual MockPolicyStore& store() { return *store_; }

  std::unique_ptr<PolicyService> service_;
};

class PolicyServiceKeyParamTest
    : public PolicyServiceTest,
      public testing::WithParamInterface<PolicyDomain> {
 public:
  using PolicyServiceTest::ExpectPersistKey;

  void SetUp() override {
    PolicyServiceTest::SetUp();
    extension_store_ = new StrictMock<MockPolicyStore>;
    service_->SetStoreForTesting(
        MakeExtensionInstallPolicyNamespace(),
        std::unique_ptr<PolicyStore>(extension_store_));
  }

  MockPolicyKey& key() override {
    return GetParam() == POLICY_DOMAIN_CHROME ? key_ : extension_install_key_;
  }

  MockPolicyStore& store() override {
    return GetParam() == POLICY_DOMAIN_CHROME ? *store_ : *extension_store_;
  }

  PolicyNamespace policy_namespace() {
    return GetParam() == POLICY_DOMAIN_CHROME
               ? MakeChromePolicyNamespace()
               : MakeExtensionInstallPolicyNamespace();
  }

  void ExpectPersistKey(Sequence* sequence) {
    PolicyServiceTest::ExpectPersistKey(key(), sequence);
  }

  void ExpectStoreFail(int flags, const std::string& code) {
    EXPECT_CALL(key(), Persist()).Times(0);
    EXPECT_CALL(store(), Set(_)).Times(0);
    EXPECT_CALL(store(), Persist()).Times(0);

    std::optional<std::string> actual_error_code;
    service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_), flags,
                    base::BindLambdaForTesting(
                        [&actual_error_code](brillo::ErrorPtr error) {
                          actual_error_code = error->GetCode();
                          return;
                        }));
    fake_loop_.Run();

    ASSERT_TRUE(actual_error_code.has_value());
    EXPECT_EQ(*actual_error_code, code);
  }

  StrictMock<MockPolicyStore>* extension_store_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(WithNamespace,
                         PolicyServiceKeyParamTest,
                         ::testing::Values(POLICY_DOMAIN_CHROME,
                                           POLICY_DOMAIN_EXTENSION_INSTALL));

const int PolicyServiceTest::kAllKeyFlags = PolicyService::KEY_ROTATE |
                                            PolicyService::KEY_INSTALL_NEW |
                                            PolicyService::KEY_CLOBBER;
const char PolicyServiceTest::kSignalSuccess[] = "success";
const char PolicyServiceTest::kSignalFailure[] = "failure";

// Parametrized fixture to test that both SHA1_RSA and SHA256_RSA are
// correctly supported.
struct PolicySignatureTypeToAlgorithm {
  em::PolicyFetchRequest::SignatureType policy_signature_type;
  crypto::SignatureVerifier::SignatureAlgorithm expected_signature_algorithm;
  PolicyDomain domain;
};
class PolicyServiceWithParameterizedPolicySignatureTypeTest
    : public PolicyServiceTest,
      public testing::WithParamInterface<PolicySignatureTypeToAlgorithm> {
 public:
  void SetUp() override {
    PolicyServiceTest::SetUp();
    extension_store_ = new StrictMock<MockPolicyStore>;
    service_->SetStoreForTesting(
        MakeExtensionInstallPolicyNamespace(),
        std::unique_ptr<PolicyStore>(extension_store_));
  }

  em::PolicyFetchRequest::SignatureType GetPolicySignatureType() {
    return GetParam().policy_signature_type;
  }

  crypto::SignatureVerifier::SignatureAlgorithm
  GetExpectedSignatureAlgorithm() {
    return GetParam().expected_signature_algorithm;
  }

  void InitPolicyWithSignatureType(const std::vector<uint8_t>& data,
                                   const std::vector<uint8_t>& signature,
                                   const std::vector<uint8_t>& key,
                                   const std::vector<uint8_t>& key_signature) {
    PolicyServiceTest::InitPolicy(data, signature, key, key_signature);
    policy_proto_.set_policy_data_signature_type(GetPolicySignatureType());
  }

  MockPolicyKey& key() override {
    return GetParam().domain == POLICY_DOMAIN_CHROME ? key_
                                                     : extension_install_key_;
  }

  MockPolicyStore& store() override {
    return GetParam().domain == POLICY_DOMAIN_CHROME ? *store_
                                                     : *extension_store_;
  }

  PolicyNamespace policy_namespace() {
    return GetParam().domain == POLICY_DOMAIN_CHROME
               ? MakeChromePolicyNamespace()
               : MakeExtensionInstallPolicyNamespace();
  }

  void ExpectPersistKey(Sequence* sequence) {
    PolicyServiceTest::ExpectPersistKey(key(), sequence);
  }

  StrictMock<MockPolicyStore>* extension_store_ = nullptr;
};

INSTANTIATE_TEST_SUITE_P(PolicyServiceWithParameterizedPolicySignatureTypeTest,
                         PolicyServiceWithParameterizedPolicySignatureTypeTest,
                         ::testing::ValuesIn<PolicySignatureTypeToAlgorithm>(
                             {{em::PolicyFetchRequest::SHA1_RSA,
                               crypto::SignatureVerifier::RSA_PKCS1_SHA1,
                               POLICY_DOMAIN_CHROME},
                              {em::PolicyFetchRequest::SHA1_RSA,
                               crypto::SignatureVerifier::RSA_PKCS1_SHA1,
                               POLICY_DOMAIN_EXTENSION_INSTALL},
                              {em::PolicyFetchRequest::SHA256_RSA,
                               crypto::SignatureVerifier::RSA_PKCS1_SHA256,
                               POLICY_DOMAIN_CHROME},
                              {em::PolicyFetchRequest::SHA256_RSA,
                               crypto::SignatureVerifier::RSA_PKCS1_SHA256,
                               POLICY_DOMAIN_EXTENSION_INSTALL}}));

TEST_P(PolicyServiceWithParameterizedPolicySignatureTypeTest, Store) {
  InitPolicyWithSignatureType(fake_data_, fake_sig_, empty_blob_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);
  EXPECT_CALL(key(),
              Verify(fake_data_, fake_sig_, GetExpectedSignatureAlgorithm()))
      .InSequence(s1, s2)
      .WillRepeatedly(Return(true));
  ExpectKeyPopulated(key(), true, &s1);
  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  kAllKeyFlags,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceWithParameterizedPolicySignatureTypeTest, StoreRotation) {
  InitPolicyWithSignatureType(fake_data_, fake_sig_, fake_key_, fake_key_sig_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);
  EXPECT_CALL(key(), Rotate(VectorEq(fake_key_), VectorEq(fake_key_sig_),
                            GetExpectedSignatureAlgorithm()))
      .InSequence(s1, s2)
      .WillOnce(Return(true));
  ExpectKeyPopulated(key(), true, &s1);
  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistKey(&s1);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  kAllKeyFlags,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, StoreWrongSignature) {
  InitPolicy(fake_data_, fake_sig_, empty_blob_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);
  EXPECT_CALL(key(), Verify(fake_data_, fake_sig_, _))
      .InSequence(s1, s2)
      .WillRepeatedly(Return(false));

  ExpectStoreFail(kAllKeyFlags, dbus_error::kVerifyFail);
}

TEST_P(PolicyServiceKeyParamTest, StoreNoData) {
  InitPolicy(empty_blob_, empty_blob_, empty_blob_, empty_blob_);

  ExpectStoreFail(kAllKeyFlags, dbus_error::kSigDecodeFail);
}

TEST_P(PolicyServiceKeyParamTest, StoreNoSignature) {
  InitPolicy(fake_data_, empty_blob_, empty_blob_, empty_blob_);

  EXPECT_CALL(key(), Verify(fake_data_, std::vector<uint8_t>(), _))
      .WillOnce(Return(false));

  ExpectStoreFail(kAllKeyFlags, dbus_error::kVerifyFail);
}

TEST_P(PolicyServiceKeyParamTest, StoreNoKey) {
  InitPolicy(fake_data_, fake_sig_, empty_blob_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), false, &s2);
  EXPECT_CALL(key(), Verify(fake_data_, fake_sig_, _))
      .InSequence(s1, s2)
      .WillRepeatedly(Return(false));

  ExpectStoreFail(kAllKeyFlags, dbus_error::kVerifyFail);
}

TEST_P(PolicyServiceKeyParamTest, StoreNewKey) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), false, &s2);
  EXPECT_CALL(key(), PopulateFromBuffer(VectorEq(fake_key_)))
      .InSequence(s1, s2)
      .WillOnce(Return(true));
  ExpectKeyPopulated(key(), true, &s1);

  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistKey(&s1);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  kAllKeyFlags,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, StoreNewKeyClobber) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), false, &s2);
  EXPECT_CALL(key(), ClobberCompromisedKey(VectorEq(fake_key_)))
      .InSequence(s1, s2)
      .WillOnce(Return(true));
  ExpectKeyPopulated(key(), true, &s1);

  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistKey(&s1);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  PolicyService::KEY_CLOBBER,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, StoreNewKeySame) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);

  Sequence s1, s2, s3;
  EXPECT_CALL(key(), Equals(BlobToString(fake_key_)))
      .InSequence(s1)
      .WillRepeatedly(Return(true));
  ExpectKeyPopulated(key(), true, &s2);
  ExpectVerifyAndSetPolicy(&s3);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  kAllKeyFlags,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, StoreNewKeyNotAllowed) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), false, &s2);

  ExpectStoreFail(PolicyService::KEY_NONE, dbus_error::kPubkeySetIllegal);
}

TEST_P(PolicyServiceKeyParamTest, StoreRotationClobber) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, fake_key_sig_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), false, &s2);
  EXPECT_CALL(key(), ClobberCompromisedKey(VectorEq(fake_key_)))
      .InSequence(s1, s2)
      .WillOnce(Return(true));
  ExpectKeyPopulated(key(), true, &s1);

  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistKey(&s1);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  PolicyService::KEY_CLOBBER,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, StoreRotationNoSignature) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);

  ExpectStoreFail(PolicyService::KEY_ROTATE, dbus_error::kPubkeySetIllegal);
}

TEST_P(PolicyServiceKeyParamTest, StoreRotationBadSignature) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, fake_key_sig_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);
  EXPECT_CALL(key(), Rotate(VectorEq(fake_key_), VectorEq(fake_key_sig_), _))
      .InSequence(s1, s2)
      .WillOnce(Return(false));

  ExpectStoreFail(PolicyService::KEY_ROTATE, dbus_error::kPubkeySetIllegal);
}

TEST_P(PolicyServiceKeyParamTest, StoreRotationNotAllowed) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, fake_key_sig_);

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);

  ExpectStoreFail(PolicyService::KEY_NONE, dbus_error::kPubkeySetIllegal);
}

TEST_P(PolicyServiceKeyParamTest, StoreRejectsSignatureTypeNone) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, empty_blob_);
  policy_proto_.set_policy_data_signature_type(
      enterprise_management::PolicyFetchRequest::NONE);

  ExpectStoreFail(PolicyService::KEY_NONE, dbus_error::kInvalidParameter);
}

TEST_P(PolicyServiceKeyParamTest, StoreDefaultsSignatureTypeToSHA1) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, fake_key_sig_);
  policy_proto_.clear_policy_data_signature_type();

  Sequence s1, s2;
  ExpectKeyEquals(key(), false, &s1);
  ExpectKeyPopulated(key(), true, &s2);
  EXPECT_CALL(key(), Verify(fake_data_, fake_sig_,
                            crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .InSequence(s1, s2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key(), Rotate(VectorEq(fake_key_), VectorEq(fake_key_sig_),
                            crypto::SignatureVerifier::RSA_PKCS1_SHA1))
      .InSequence(s1, s2)
      .WillOnce(Return(true));
  ExpectKeyPopulated(key(), true, &s1);

  ExpectVerifyAndSetPolicy(&s2);
  ExpectPersistKey(&s1);
  ExpectPersistPolicy(&s2);

  service_->Store(policy_namespace(), SerializeAsBlob(policy_proto_),
                  kAllKeyFlags,
                  MockPolicyService::CreateExpectSuccessCallback());

  fake_loop_.Run();
}

TEST_P(PolicyServiceKeyParamTest, Retrieve) {
  InitPolicy(fake_data_, fake_sig_, fake_key_, fake_key_sig_);

  EXPECT_CALL(store(), Get()).WillOnce(ReturnRef(policy_proto_));

  std::vector<uint8_t> out_policy_blob;
  EXPECT_TRUE(service_->Retrieve(policy_namespace(), &out_policy_blob));
  EXPECT_EQ(SerializeAsBlob(policy_proto_), out_policy_blob);
}

TEST_P(PolicyServiceKeyParamTest, PersistPolicySuccess) {
  EXPECT_CALL(store(), Persist()).WillOnce(Return(true));
  EXPECT_CALL(delegate_, OnPolicyPersisted(true)).Times(1);
  service_->PersistPolicy(policy_namespace(), PolicyService::Completion());
}

TEST_P(PolicyServiceKeyParamTest, PersistPolicyFailure) {
  EXPECT_CALL(store(), Persist()).WillOnce(Return(false));
  EXPECT_CALL(delegate_, OnPolicyPersisted(false)).Times(1);
  service_->PersistPolicy(policy_namespace(), PolicyService::Completion());
}

// Tests PolicyService with multiple namespace and a real PolicyStore.
class PolicyServiceNamespaceTest : public testing::Test {
 public:
  PolicyServiceNamespaceTest() = default;

  void SetUp() override {
    base::FilePath temp_dir("/tmp/policy_dir");
    ASSERT_TRUE(system_utils_.CreateDir(temp_dir));
    fake_loop_.SetAsCurrent();
    service_ = std::make_unique<PolicyService>(
        &system_utils_,
        /*policy_dir=*/temp_dir,
        /*policy_key=*/&key_,
        /*extension_install_policy_key=*/&extension_install_key_,
        /*metrics=*/nullptr,
        /*resilient_chrome_policy_store=*/false);

    const std::string kExtensionId1 = "abcdefghijklmnopabcdefghijklmnop";
    ns1_ = PolicyNamespace(POLICY_DOMAIN_CHROME, "");
    ns2_ = PolicyNamespace(POLICY_DOMAIN_EXTENSIONS, kExtensionId1);
    ns3_ = PolicyNamespace(POLICY_DOMAIN_EXTENSION_INSTALL, "");

    policy_path1_ = temp_dir.Append(PolicyService::kChromePolicyFileName);
    policy_path2_ = temp_dir.Append(
        PolicyService::kExtensionsPolicyFileNamePrefix + kExtensionId1);
    policy_path3_ =
        temp_dir.Append(PolicyService::kExtensionInstallPolicyFileName);
  }

 protected:
  std::vector<uint8_t> PolicyValueToBlob(const std::string& policy_value) {
    em::PolicyFetchResponse policy_response;
    em::PolicyData policy_data;
    policy_data.set_policy_value(policy_value);
    EXPECT_TRUE(
        policy_data.SerializeToString(policy_response.mutable_policy_data()));
    return StringToBlob(policy_response.SerializeAsString());
  }

  std::string BlobToPolicyValue(const std::vector<uint8_t>& policy_blob) {
    em::PolicyFetchResponse policy_response;
    em::PolicyData policy_data;
    EXPECT_TRUE(
        policy_response.ParseFromArray(policy_blob.data(), policy_blob.size()));
    EXPECT_TRUE(policy_data.ParseFromString(policy_response.policy_data()));
    return policy_data.policy_value();
  }

  // Stores policy with value |policy_value| in the namespace |ns|.
  void StorePolicy(const std::string& policy_value, const PolicyNamespace& ns) {
    const std::vector<uint8_t> policy_blob = PolicyValueToBlob(policy_value);
    if (ns == ns3_) {
      EXPECT_CALL(extension_install_key_, Verify(_, _, _))
          .WillRepeatedly(Return(true));
    } else {
      EXPECT_CALL(key_, Verify(_, _, _)).WillRepeatedly(Return(true));
    }
    service_->Store(ns, policy_blob, PolicyService::KEY_NONE,
                    MockPolicyService::CreateExpectSuccessCallback());
  }

  // Retrieves the policy value from namespace |ns|. Returns an empty string on
  // error.
  std::string RetrievePolicy(const PolicyNamespace& ns) {
    std::vector<uint8_t> policy_blob;
    if (!service_->Retrieve(ns, &policy_blob)) {
      return std::string();
    }
    return BlobToPolicyValue(policy_blob);
  }

  // Loads a policy value from disk and returns the policy value string. Returns
  // an empty string on error.
  std::string LoadPolicyFromFile(const base::FilePath& policy_path) {
    std::optional<std::vector<uint8_t>> blob =
        system_utils_.ReadFileToBytes(policy_path);
    if (!blob.has_value()) {
      return std::string();
    }
    return BlobToPolicyValue(*blob);
  }

  // Saves a policy value to disk embedded in a PolicyFetchResponse.
  void SavePolicyToFile(const base::FilePath& policy_path,
                        const std::string& policy_value) {
    EXPECT_TRUE(
        system_utils_.EnsureFile(policy_path, PolicyValueToBlob(policy_value)));
  }

  brillo::FakeMessageLoop fake_loop_{nullptr};
  FakeSystemUtils system_utils_;
  std::unique_ptr<PolicyService> service_;
  StrictMock<MockPolicyKey> key_;
  StrictMock<MockPolicyKey> extension_install_key_;
  PolicyNamespace ns1_;
  PolicyNamespace ns2_;
  PolicyNamespace ns3_;
  base::FilePath policy_path1_;
  base::FilePath policy_path2_;
  base::FilePath policy_path3_;
};

TEST_F(PolicyServiceNamespaceTest, Store) {
  EXPECT_FALSE(system_utils_.Exists(policy_path1_));
  StorePolicy(kPolicyValue1, ns1_);
  // The file is stored in a "background" task.
  fake_loop_.Run();
  EXPECT_TRUE(system_utils_.Exists(policy_path1_));
  std::string actual_value = LoadPolicyFromFile(policy_path1_);
  EXPECT_EQ(kPolicyValue1, actual_value);
}

TEST_F(PolicyServiceNamespaceTest, StoreMultiple) {
  EXPECT_FALSE(system_utils_.Exists(policy_path1_));
  StorePolicy(kPolicyValue1, ns1_);
  fake_loop_.Run();
  EXPECT_TRUE(system_utils_.Exists(policy_path1_));

  EXPECT_FALSE(system_utils_.Exists(policy_path2_));
  StorePolicy(kPolicyValue2, ns2_);
  fake_loop_.Run();
  EXPECT_TRUE(system_utils_.Exists(policy_path2_));

  EXPECT_FALSE(system_utils_.Exists(policy_path3_));
  StorePolicy(kPolicyValue3, ns3_);
  fake_loop_.Run();
  EXPECT_TRUE(system_utils_.Exists(policy_path3_));

  std::string actual_value1 = LoadPolicyFromFile(policy_path1_);
  std::string actual_value2 = LoadPolicyFromFile(policy_path2_);
  std::string actual_value3 = LoadPolicyFromFile(policy_path3_);

  EXPECT_EQ(kPolicyValue1, actual_value1);
  EXPECT_EQ(kPolicyValue2, actual_value2);
  EXPECT_EQ(kPolicyValue3, actual_value3);
}

TEST_F(PolicyServiceNamespaceTest, StoreRetrieveMultiple) {
  EXPECT_FALSE(system_utils_.Exists(policy_path1_));
  EXPECT_FALSE(system_utils_.Exists(policy_path2_));
  EXPECT_FALSE(system_utils_.Exists(policy_path3_));

  StorePolicy(kPolicyValue1, ns1_);
  StorePolicy(kPolicyValue2, ns2_);
  StorePolicy(kPolicyValue3, ns3_);

  std::string actual_value1 = RetrievePolicy(ns1_);
  std::string actual_value2 = RetrievePolicy(ns2_);
  std::string actual_value3 = RetrievePolicy(ns3_);

  EXPECT_EQ(kPolicyValue1, actual_value1);
  EXPECT_EQ(kPolicyValue2, actual_value2);
  EXPECT_EQ(kPolicyValue3, actual_value3);

  // The files are stored in a "background" task.
  fake_loop_.Run();

  EXPECT_TRUE(system_utils_.Exists(policy_path1_));
  EXPECT_TRUE(system_utils_.Exists(policy_path2_));
  EXPECT_TRUE(system_utils_.Exists(policy_path3_));
}

TEST_F(PolicyServiceNamespaceTest, LoadPolicyFromDisk) {
  // Makes sure that policy is loaded from disk on first access.
  SavePolicyToFile(policy_path1_, kPolicyValue1);
  const std::string actual_value_1 = RetrievePolicy(ns1_);
  EXPECT_EQ(kPolicyValue1, actual_value_1);

  // Makes sure that policy is loaded from disk on first access.
  SavePolicyToFile(policy_path3_, kPolicyValue3);
  const std::string actual_value_3 = RetrievePolicy(ns3_);
  EXPECT_EQ(kPolicyValue3, actual_value_3);
}

}  // namespace login_manager
