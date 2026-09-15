// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/session.h"

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <chaps/proto_bindings/ck_structs.pb.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <dbus/chaps/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/frontend/chaps/mock_frontend.h>
#include <metrics/metrics_library_mock.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include "chaps/chaps.h"
#include "chaps/chaps_factory_impl.h"
#include "chaps/chaps_factory_mock.h"
#include "chaps/chaps_utility.h"
#include "chaps/handle_generator_mock.h"
#include "chaps/object.h"
#include "chaps/object_impl.h"
#include "chaps/object_mock.h"
#include "chaps/object_pool_mock.h"
#include "chaps/proto_conversion.h"
#include "chaps/session_impl.h"
#if USE_TPM2
#include "libhwsec/factory/tpm2_simulator_factory_for_test.h"
#endif
#include "libhwsec/frontend/chaps/frontend.h"

using ::hwsec::TPMError;
using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;
using ::std::string;
using ::std::vector;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using Result = chaps::ObjectPool::Result;
using TPMRetryAction = ::hwsec::TPMRetryAction;

namespace {

hwsec::ECCPublicInfo GenerateECCPublicInfo() {
  hwsec_foundation::ScopedBN_CTX context =
      hwsec_foundation::CreateBigNumContext();

  std::optional<hwsec_foundation::EllipticCurve> ec_256 =
      hwsec_foundation::EllipticCurve::Create(
          hwsec_foundation::EllipticCurve::CurveType::kPrime256, context.get());

  CHECK(ec_256.has_value());

  crypto::ScopedEC_KEY key = ec_256->GenerateKey(context.get());

  crypto::ScopedBIGNUM x(BN_new()), y(BN_new());
  CHECK_NE(x, nullptr);
  CHECK_NE(y, nullptr);

  const EC_POINT* ec_point = EC_KEY_get0_public_key(key.get());
  CHECK_NE(ec_point, nullptr);
  CHECK(EC_POINT_get_affine_coordinates_GFp(ec_256->GetGroup(), ec_point,
                                            x.get(), y.get(), nullptr));

  brillo::Blob x_point =
      brillo::BlobFromString(chaps::ConvertFromBIGNUM(x.get()));
  brillo::Blob y_point =
      brillo::BlobFromString(chaps::ConvertFromBIGNUM(y.get()));
  return hwsec::ECCPublicInfo{
      .nid = NID_X9_62_prime256v1,
      .x_point = x_point,
      .y_point = y_point,
  };
}

void ConfigureObjectPool(chaps::ObjectPoolMock* op, int handle_base) {
  op->SetupFake(handle_base);
  EXPECT_CALL(*op, Insert(_)).Times(AnyNumber());
  EXPECT_CALL(*op, Find(_, _)).Times(AnyNumber());
  EXPECT_CALL(*op, FindByHandle(_, testing::A<const chaps::Object**>()))
      .Times(AnyNumber());
  EXPECT_CALL(
      *op, FindByHandle(_, testing::A<std::shared_ptr<const chaps::Object>*>()))
      .Times(AnyNumber());
  EXPECT_CALL(*op, Delete(_)).Times(AnyNumber());
  EXPECT_CALL(*op, Flush(_)).WillRepeatedly(Return(Result::Success));
}

chaps::ObjectPool* CreateObjectPoolMock() {
  chaps::ObjectPoolMock* op = new chaps::ObjectPoolMock();
  ConfigureObjectPool(op, 100);
  return op;
}

chaps::Object* CreateObjectMock() {
  chaps::ObjectMock* o = new chaps::ObjectMock();
  o->SetupFake();
  EXPECT_CALL(*o, GetObjectClass()).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributes(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, FinalizeNewObject()).WillRepeatedly(Return(CKR_OK));
  EXPECT_CALL(*o, Copy(_)).WillRepeatedly(Return(CKR_OK));
  EXPECT_CALL(*o, IsTokenObject()).Times(AnyNumber());
  EXPECT_CALL(*o, IsAttributePresent(_)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeString(_)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeInt(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeBool(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeString(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeInt(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeBool(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, set_handle(_)).Times(AnyNumber());
  EXPECT_CALL(*o, set_store_id(_)).Times(AnyNumber());
  EXPECT_CALL(*o, handle()).Times(AnyNumber());
  EXPECT_CALL(*o, store_id()).Times(AnyNumber());
  EXPECT_CALL(*o, RemoveAttribute(_)).Times(AnyNumber());
  return o;
}

void ConfigureHwsec(hwsec::MockChapsFrontend* hwsec) {
  EXPECT_CALL(*hwsec, GetRandomBlob(_)).WillRepeatedly([](size_t size) {
    return brillo::Blob(size);
  });
  EXPECT_CALL(*hwsec, GetRandomSecureBlob(_)).WillRepeatedly([](size_t size) {
    return brillo::SecureBlob(size);
  });
}

string bn2bin(const BIGNUM* bn) {
  string bin;
  bin.resize(BN_num_bytes(bn));
  bin.resize(BN_bn2bin(bn, chaps::ConvertStringToByteBuffer(bin.data())));
  return bin;
}

std::string GetRSAPSSParam(CK_MECHANISM_TYPE hashAlg,
                           CK_RSA_PKCS_MGF_TYPE mgf,
                           CK_ULONG sLen) {
  CK_RSA_PKCS_PSS_PARAMS params;
  params.hashAlg = hashAlg;
  params.mgf = mgf;
  params.sLen = sLen;
  std::string param_bytes(reinterpret_cast<char*>(&params), sizeof(params));
  return param_bytes;
}

}  // namespace

namespace chaps {

const char kChapsSessionDecrypt[] = "Platform.Chaps.Session.Decrypt";
const char kChapsSessionDigest[] = "Platform.Chaps.Session.Digest";
const char kChapsSessionEncrypt[] = "Platform.Chaps.Session.Encrypt";
const char kChapsSessionSign[] = "Platform.Chaps.Session.Sign";
const char kChapsSessionVerify[] = "Platform.Chaps.Session.Verify";
const char kChapsSessionDeriveKey[] = "Platform.Chaps.Session.DeriveKey";
const char kChapsSessionWrapKey[] = "Platform.Chaps.Session.WrapKey";
const char kChapsSessionUnwrapKey[] = "Platform.Chaps.Session.UnwrapKey";
#if USE_TPM2
const char kChapsSessionWrapKeyWithChaps[] =
    "Platform.Chaps.Session.WrapKeyWithChaps";
const char kChapsSessionUnwrapKeyWithChaps[] =
    "Platform.Chaps.Session.UnwrapKeyWithChaps";
#endif  // USE_TPM2
const char kChapsRandomSeed[] = "random_seed";

// Test fixture for an initialized SessionImpl instance.
class TestSession : public ::testing::Test {
 public:
  TestSession() {
    EXPECT_CALL(factory_, CreateObject())
        .WillRepeatedly(InvokeWithoutArgs(CreateObjectMock));
    EXPECT_CALL(factory_, CreateObjectPool(_, _, _))
        .WillRepeatedly(InvokeWithoutArgs(CreateObjectPoolMock));
    EXPECT_CALL(handle_generator_, CreateHandle()).WillRepeatedly(Return(1));
    ConfigureObjectPool(&token_pool_, 0);
    ConfigureHwsec(&hwsec_);
  }
  void SetUp() {
    chaps_metrics_.set_metrics_library_for_testing(&mock_metrics_library_);
    session_.reset(new SessionImpl(1, &token_pool_, &hwsec_, &factory_,
                                   &handle_generator_, false, &chaps_metrics_));
  }
  void GenerateSecretKey(CK_MECHANISM_TYPE mechanism,
                         CK_ULONG size,
                         const Object** obj) {
    CK_BBOOL no = CK_FALSE;
    CK_BBOOL yes = CK_TRUE;
    CK_ATTRIBUTE encdec_template[] = {{CKA_TOKEN, &no, sizeof(no)},
                                      {CKA_ENCRYPT, &yes, sizeof(yes)},
                                      {CKA_DECRYPT, &yes, sizeof(yes)},
                                      {CKA_VALUE_LEN, &size, sizeof(size)}};
    CK_ATTRIBUTE signverify_template[] = {{CKA_TOKEN, &no, sizeof(no)},
                                          {CKA_SIGN, &yes, sizeof(yes)},
                                          {CKA_VERIFY, &yes, sizeof(yes)},
                                          {CKA_VALUE_LEN, &size, sizeof(size)}};
    CK_ATTRIBUTE_PTR attr = encdec_template;
    if (mechanism == CKM_GENERIC_SECRET_KEY_GEN) {
      attr = signverify_template;
    }
    int handle = 0;
    ASSERT_EQ(CKR_OK, session_->GenerateKey(mechanism, "", attr, 4, &handle));
    ASSERT_TRUE(session_->GetObject(handle, obj));
  }
  void GenerateRSAKeyPair(bool signing,
                          CK_ULONG size,
                          const Object** pub,
                          const Object** priv) {
    CK_BBOOL no = CK_FALSE;
    CK_BBOOL yes = CK_TRUE;
    CK_BYTE pubexp[] = {1, 0, 1};
    CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                               {CKA_ENCRYPT, signing ? &no : &yes, sizeof(no)},
                               {CKA_VERIFY, signing ? &yes : &no, sizeof(no)},
                               {CKA_PUBLIC_EXPONENT, pubexp, 3},
                               {CKA_MODULUS_BITS, &size, sizeof(size)}};
    CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &no, sizeof(CK_BBOOL)},
                                {CKA_DECRYPT, signing ? &no : &yes, sizeof(no)},
                                {CKA_SIGN, signing ? &yes : &no, sizeof(no)}};
    int pubh = 0, privh = 0;
    ASSERT_EQ(CKR_OK,
              session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                        std::size(pub_attr), priv_attr,
                                        std::size(priv_attr), &pubh, &privh));
    ASSERT_TRUE(session_->GetObject(pubh, pub));
    ASSERT_TRUE(session_->GetObject(privh, priv));
  }

  string GetDERforNID(int openssl_nid) {
    // OBJ_nid2obj returns a pointer to an internal table and does not allocate
    // memory. No need to free.
    ASN1_OBJECT* obj = OBJ_nid2obj(NID_X9_62_prime256v1);
    int expected_size = i2d_ASN1_OBJECT(obj, nullptr);
    string output(expected_size, '\0');
    unsigned char* buf = ConvertStringToByteBuffer(output.data());
    int output_size = i2d_ASN1_OBJECT(obj, &buf);
    CHECK_EQ(output_size, expected_size);
    return output;
  }

  void GenerateECCKeyPair(bool use_token_object_for_pub,
                          bool use_token_object_for_priv,
                          const Object** pub,
                          const Object** priv) {
    // Create DER encoded OID of P-256 for CKA_EC_PARAMS (prime256v1 or
    // secp256r1)
    string ec_params = GetDERforNID(NID_X9_62_prime256v1);

    CK_BBOOL no = CK_FALSE;
    CK_BBOOL yes = CK_TRUE;
    CK_ATTRIBUTE pub_attr[] = {
        {CKA_TOKEN, use_token_object_for_pub ? &yes : &no, sizeof(CK_BBOOL)},
        {CKA_ENCRYPT, &no, sizeof(CK_BBOOL)},
        {CKA_VERIFY, &yes, sizeof(CK_BBOOL)},
        {CKA_EC_PARAMS, ConvertStringToByteBuffer(ec_params.data()),
         ec_params.size()}};
    CK_ATTRIBUTE priv_attr[] = {
        {CKA_TOKEN, use_token_object_for_priv ? &yes : &no, sizeof(CK_BBOOL)},
        {CKA_DECRYPT, &no, sizeof(CK_BBOOL)},
        {CKA_SIGN, &yes, sizeof(CK_BBOOL)}};
    int pubh = 0, privh = 0;
    ASSERT_EQ(CKR_OK,
              session_->GenerateKeyPair(CKM_EC_KEY_PAIR_GEN, "", pub_attr,
                                        std::size(pub_attr), priv_attr,
                                        std::size(priv_attr), &pubh, &privh));
    ASSERT_TRUE(session_->GetObject(pubh, pub));
    ASSERT_TRUE(session_->GetObject(privh, priv));
  }

  int CreateObject() {
    CK_OBJECT_CLASS c = CKO_DATA;
    CK_BBOOL no = CK_FALSE;
    CK_ATTRIBUTE attr[] = {{CKA_CLASS, &c, sizeof(c)},
                           {CKA_TOKEN, &no, sizeof(no)}};
    int h;
    session_->CreateObject(attr, 2, &h);
    return h;
  }

  void TestSignVerify(const Object* pub,
                      const Object* priv,
                      size_t input_size,
                      CK_MECHANISM_TYPE mech,
                      const string& mechanism_parameter) {
    string in(input_size, 'A');
    int len = 0;
    string sig;

    // Sign / verify with OperationSinglePart().
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kSign, mech, mechanism_parameter, priv));
    EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
              session_->OperationSinglePart(kSign, in, &len, &sig));
    EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kSign, in, &len, &sig));
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kVerify, mech, mechanism_parameter, pub));
    EXPECT_EQ(CKR_OK, session_->OperationUpdate(kVerify, in, nullptr, nullptr));
    EXPECT_EQ(CKR_OK, session_->VerifyFinal(sig));

    // Same stuff with OperationUpdate().
    in = string(input_size, 'B');
    string sig2;
    len = 0;
    size_t first_divide = input_size / 2;
    size_t second_divide = input_size / 5 * 2;
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kSign, mech, mechanism_parameter, priv));
    EXPECT_EQ(CKR_OK, session_->OperationUpdate(
                          kSign, in.substr(0, first_divide), nullptr, nullptr));
    EXPECT_EQ(CKR_OK,
              session_->OperationUpdate(
                  kSign, in.substr(first_divide, input_size - first_divide),
                  nullptr, nullptr));
    EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
              session_->OperationFinal(kSign, &len, &sig2));
    EXPECT_EQ(CKR_OK, session_->OperationFinal(kSign, &len, &sig2));

    // Test verification with OperationUpdate().
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kVerify, mech, mechanism_parameter, pub));
    EXPECT_EQ(CKR_OK,
              session_->OperationUpdate(kVerify, in.substr(0, second_divide),
                                        nullptr, nullptr));
    EXPECT_EQ(CKR_OK,
              session_->OperationUpdate(
                  kVerify, in.substr(second_divide, input_size - second_divide),
                  nullptr, nullptr));
    EXPECT_EQ(CKR_OK, session_->VerifyFinal(sig2));
  }

  hwsec::ScopedKey GetTestScopedKey() {
    return hwsec::ScopedKey(hwsec::Key{.token = 42},
                            hwsec_.GetFakeMiddlewareDerivative());
  }

 protected:
  ObjectPoolMock token_pool_;
  ChapsFactoryMock factory_;
  StrictMock<hwsec::MockChapsFrontend> hwsec_;
  HandleGeneratorMock handle_generator_;
  std::unique_ptr<SessionImpl> session_;
  StrictMock<MetricsLibraryMock> mock_metrics_library_;
  ChapsMetrics chaps_metrics_;
  const brillo::SecureBlob random_seed_ = brillo::SecureBlob(kChapsRandomSeed);
};

// Session Test that uses real Object implementation (ObjectImpl)
class TestSessionWithRealObject : public TestSession {
 public:
  TestSessionWithRealObject() {
    chaps::ChapsFactory* factory = &factory_;
    EXPECT_CALL(factory_, CreateObject())
        .WillRepeatedly(InvokeWithoutArgs(
            [factory] { return new chaps::ObjectImpl(factory); }));
    EXPECT_CALL(factory_, CreateObjectPool(_, _, _))
        .WillRepeatedly(InvokeWithoutArgs(CreateObjectPoolMock));
    EXPECT_CALL(factory_, CreateObjectPolicy(_))
        .WillRepeatedly(Invoke(ChapsFactoryImpl::GetObjectPolicyForType));
    EXPECT_CALL(factory_, GetRandomSeed())
        .WillRepeatedly(ReturnRef(random_seed_));
    EXPECT_CALL(handle_generator_, CreateHandle()).WillRepeatedly(Return(1));
    ConfigureObjectPool(&token_pool_, 0);
    ConfigureHwsec(&hwsec_);
  }
};

#if USE_TPM2
// Session Test that uses real Object implementation (ObjectImpl)
class TestSessionWithTpmSimulator : public TestSessionWithRealObject {
 public:
  using TestSessionWithRealObject::TestSessionWithRealObject;
  void SetUp() override {
    chaps_metrics_.set_metrics_library_for_testing(&mock_metrics_library_);
    session_.reset();
    session_ = std::make_unique<SessionImpl>(
        1, &token_pool_, hwsec_simulator_.get(), &factory_, &handle_generator_,
        false, &chaps_metrics_);
  }

 private:
  hwsec::Tpm2SimulatorFactoryForTest hwsec_simulator_factory_;
  std::unique_ptr<const hwsec::ChapsFrontend> hwsec_simulator_ =
      hwsec_simulator_factory_.GetChapsFrontend();
};
#endif  // USE_TPM2

typedef TestSession TestSession_DeathTest;

// Test that SessionImpl asserts as expected when not properly initialized.
TEST(DeathTest, InvalidInit) {
  ObjectPoolMock pool;
  ChapsFactoryMock factory;
  hwsec::MockChapsFrontend hwsec;
  HandleGeneratorMock handle_generator;
  SessionImpl* session;
  ChapsMetrics chaps_metrics;
  EXPECT_CALL(factory, CreateObjectPool(_, _, _)).Times(AnyNumber());
  EXPECT_DEATH_IF_SUPPORTED(
      session = new SessionImpl(1, nullptr, &hwsec, &factory, &handle_generator,
                                false, &chaps_metrics),
      "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session = new SessionImpl(1, &pool, &hwsec, nullptr, &handle_generator,
                                false, &chaps_metrics),
      "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session = new SessionImpl(1, &pool, &hwsec, &factory, nullptr, false,
                                &chaps_metrics),
      "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session = new SessionImpl(1, &pool, &hwsec, &factory, &handle_generator,
                                false, nullptr),
      "Check failed");
  (void)session;
}

// Test that SessionImpl asserts as expected when passed invalid arguments.
TEST_F(TestSession_DeathTest, InvalidArgs) {
  OperationType invalid_op = kNumOperationTypes;
  EXPECT_DEATH_IF_SUPPORTED(session_->IsOperationActive(invalid_op),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->CreateObject(nullptr, 0, nullptr),
                            "Check failed");
  int i;
  EXPECT_DEATH_IF_SUPPORTED(session_->CreateObject(nullptr, 1, &i),
                            "Check failed");
  i = CreateObject();
  EXPECT_DEATH_IF_SUPPORTED(session_->CopyObject(nullptr, 0, i, nullptr),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->CopyObject(nullptr, 1, i, &i),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->FindObjects(invalid_op, nullptr),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->GetObject(1, nullptr), "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->OperationInit(invalid_op, 0, "", nullptr),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session_->OperationInit(kEncrypt, CKM_AES_CBC, "", nullptr),
      "Check failed");
  string s;
  const Object* o;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &o);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt, static_cast<int>(CKR_OK)))
      .WillOnce(Return(true));
  ASSERT_EQ(CKR_OK, session_->OperationInit(kEncrypt, CKM_AES_ECB, "", o));
  EXPECT_DEATH_IF_SUPPORTED(session_->OperationUpdate(invalid_op, "", &i, &s),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session_->OperationUpdate(kEncrypt, "", nullptr, &s), "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session_->OperationUpdate(kEncrypt, "", &i, nullptr), "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->OperationFinal(invalid_op, &i, &s),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->OperationFinal(kEncrypt, nullptr, &s),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->OperationFinal(kEncrypt, &i, nullptr),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      session_->OperationSinglePart(invalid_op, "", &i, &s), "Check failed");
}

// Test that SessionImpl asserts when out-of-memory during initialization.
TEST(DeathTest, OutOfMemoryInit) {
  ObjectPoolMock pool;
  hwsec::MockChapsFrontend hwsec;
  ChapsFactoryMock factory;
  HandleGeneratorMock handle_generator;
  ObjectPool* null_pool = nullptr;
  ChapsMetrics chaps_metrics;
  EXPECT_CALL(factory, CreateObjectPool(_, _, _))
      .WillRepeatedly(Return(null_pool));
  Session* session;
  EXPECT_DEATH_IF_SUPPORTED(
      session = new SessionImpl(1, &pool, &hwsec, &factory, &handle_generator,
                                false, &chaps_metrics),
      "Check failed");
  (void)session;
}

// Test that SessionImpl asserts when out-of-memory during object creation.
TEST_F(TestSession_DeathTest, OutOfMemoryObject) {
  Object* null_object = nullptr;
  EXPECT_CALL(factory_, CreateObject()).WillRepeatedly(Return(null_object));
  int tmp;
  EXPECT_DEATH_IF_SUPPORTED(session_->CreateObject(nullptr, 0, &tmp),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(session_->FindObjectsInit(nullptr, 0),
                            "Check failed");
}

// Test that default session properties are correctly reported.
TEST_F(TestSession, DefaultSetup) {
  EXPECT_EQ(1, session_->GetSlot());
  EXPECT_FALSE(session_->IsReadOnly());
  EXPECT_FALSE(session_->IsOperationActive(kEncrypt));
}

// Test object management: create / copy / find / destroy.
TEST_F(TestSession, Objects) {
  EXPECT_CALL(token_pool_, Insert(_)).Times(2);
  EXPECT_CALL(token_pool_, Find(_, _)).Times(1);
  EXPECT_CALL(token_pool_, Delete(_)).Times(1);
  CK_OBJECT_CLASS oc = CKO_SECRET_KEY;
  CK_ATTRIBUTE attr[] = {{CKA_CLASS, &oc, sizeof(oc)}};
  int handle = 0;
  int invalid_handle = -1;
  // Create a new object.
  ASSERT_EQ(CKR_OK, session_->CreateObject(attr, std::size(attr), &handle));
  EXPECT_GT(handle, 0);
  const Object* o;
  // Get the new object from the new handle.
  EXPECT_TRUE(session_->GetObject(handle, &o));
  int handle2 = 0;
  // Copy an object (try invalid and valid handles).
  EXPECT_EQ(
      CKR_OBJECT_HANDLE_INVALID,
      session_->CopyObject(attr, std::size(attr), invalid_handle, &handle2));
  ASSERT_EQ(CKR_OK,
            session_->CopyObject(attr, std::size(attr), handle, &handle2));
  // Ensure handles are unique.
  EXPECT_TRUE(handle != handle2);
  EXPECT_TRUE(session_->GetObject(handle2, &o));
  EXPECT_FALSE(session_->GetObject(invalid_handle, &o));
  vector<int> v;
  // Find objects with calls out-of-order.
  EXPECT_EQ(CKR_OPERATION_NOT_INITIALIZED, session_->FindObjects(1, &v));
  EXPECT_EQ(CKR_OPERATION_NOT_INITIALIZED, session_->FindObjectsFinal());
  // Find the objects we've created (there should be 2).
  EXPECT_EQ(CKR_OK, session_->FindObjectsInit(attr, std::size(attr)));
  EXPECT_EQ(CKR_OPERATION_ACTIVE,
            session_->FindObjectsInit(attr, std::size(attr)));
  // Test multi-step finds by only allowing 1 result at a time.
  EXPECT_EQ(CKR_OK, session_->FindObjects(1, &v));
  EXPECT_EQ(1, v.size());
  EXPECT_EQ(CKR_OK, session_->FindObjects(1, &v));
  EXPECT_EQ(2, v.size());
  // We have them all but we'll query again to make sure it behaves properly.
  EXPECT_EQ(CKR_OK, session_->FindObjects(1, &v));
  ASSERT_EQ(2, v.size());
  // Check that the handles found are the same ones we know about.
  EXPECT_TRUE(v[0] == handle || v[1] == handle);
  EXPECT_TRUE(v[0] == handle2 || v[1] == handle2);
  EXPECT_EQ(CKR_OK, session_->FindObjectsFinal());
  // Destroy an object (try invalid and valid handles).
  EXPECT_EQ(CKR_OBJECT_HANDLE_INVALID, session_->DestroyObject(invalid_handle));
  EXPECT_EQ(CKR_OK, session_->DestroyObject(handle));
  // Once destroyed, we should not be able to use the handle.
  EXPECT_FALSE(session_->GetObject(handle, &o));
}

// Test multi-part and single-part cipher operations.
TEST_F(TestSession, Cipher) {
  const Object* key_object = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &key_object);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt, static_cast<int>(CKR_OK)))
      .Times(3);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kEncrypt, CKM_AES_CBC_PAD,
                                            string(16, 'A'), key_object));
  string in(22, 'B');
  string out, tmp;
  int maxlen = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(2);
  // Check buffer-too-small semantics (and for each call following).
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationUpdate(kEncrypt, in, &maxlen, &tmp));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kEncrypt, in, &maxlen, &tmp));
  out += tmp;
  // The first block is ready, check that we've received it.
  EXPECT_EQ(16, out.length());
  maxlen = 0;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationFinal(kEncrypt, &maxlen, &tmp));
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kEncrypt, &maxlen, &tmp));
  out += tmp;
  // Check that we've received the final block.
  EXPECT_EQ(32, out.length());
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDecrypt, static_cast<int>(CKR_OK)))
      .Times(2);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDecrypt, CKM_AES_CBC_PAD,
                                            string(16, 'A'), key_object));
  string in2;
  maxlen = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDecrypt,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kDecrypt, out, &maxlen, &in2));
  EXPECT_EQ(CKR_OK,
            session_->OperationSinglePart(kDecrypt, out, &maxlen, &in2));
  EXPECT_EQ(22, in2.length());
  // Check that what has been decrypted matches our original plain-text.
  EXPECT_TRUE(in == in2);
}

// Test multi-part and single-part digest operations.
TEST_F(TestSession, Digest) {
  string in(30, 'A');
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest, static_cast<int>(CKR_OK)))
      .Times(7);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDigest, in.substr(0, 10),
                                              nullptr, nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDigest, in.substr(10, 10),
                                              nullptr, nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDigest, in.substr(20, 10),
                                              nullptr, nullptr));
  int len = 0;
  string out;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(2);
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationFinal(kDigest, &len, &out));
  EXPECT_EQ(20, len);
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kDigest, &len, &out));
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  string out2;
  len = 0;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kDigest, in, &len, &out2));
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kDigest, in, &len, &out2));
  EXPECT_EQ(20, len);
  // Check that both operations computed the same digest.
  EXPECT_TRUE(out == out2);
}

// Test HMAC sign and verify operations.
TEST_F(TestSession, HMAC) {
  const Object* key_object = nullptr;
  GenerateSecretKey(CKM_GENERIC_SECRET_KEY_GEN, 32, &key_object);
  string in(30, 'A');
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .Times(5);
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_SHA256_HMAC, "", key_object));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kSign, in.substr(0, 10), nullptr,
                                              nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kSign, in.substr(10, 10), nullptr,
                                              nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kSign, in.substr(20, 10), nullptr,
                                              nullptr));
  int len = 0;
  string out;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL, session_->OperationFinal(kSign, &len, &out));
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kSign, &len, &out));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionVerify, static_cast<int>(CKR_OK)))
      .Times(3);
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kVerify, CKM_SHA256_HMAC, "", key_object));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kVerify, in, nullptr, nullptr));
  // A successful verify implies both operations computed the same MAC.
  EXPECT_EQ(CKR_OK, session_->VerifyFinal(out));
}

// Test empty multi-part operation.
TEST_F(TestSession, FinalWithNoUpdate) {
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest, static_cast<int>(CKR_OK)))
      .Times(2);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  int len = 20;
  string out;
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kDigest, &len, &out));
  EXPECT_EQ(20, len);
}

// Test multi-part and single-part operations inhibit each other.
TEST_F(TestSession, UpdateOperationPreventsSinglePart) {
  string in(30, 'A');
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest, static_cast<int>(CKR_OK)))
      .Times(2);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDigest, in.substr(0, 10),
                                              nullptr, nullptr));
  int len = 0;
  string out;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_ACTIVE)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_OPERATION_ACTIVE, session_->OperationSinglePart(
                                      kDigest, in.substr(10, 20), &len, &out));

  // The error also terminates the operation.
  len = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_NOT_INITIALIZED)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_OPERATION_NOT_INITIALIZED,
            session_->OperationFinal(kDigest, &len, &out));
}

TEST_F(TestSession, SinglePartOperationPreventsUpdate) {
  string in(30, 'A');
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest, static_cast<int>(CKR_OK)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  string out;
  int len = 0;
  // Perform a single part operation but leave the output to be collected.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kDigest, in, &len, &out));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_ACTIVE)))
      .WillOnce(Return(true));
  EXPECT_EQ(
      CKR_OPERATION_ACTIVE,
      session_->OperationUpdate(kDigest, in.substr(10, 10), nullptr, nullptr));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_NOT_INITIALIZED)))
      .WillOnce(Return(true));
  // The error also terminates the operation.
  EXPECT_EQ(CKR_OPERATION_NOT_INITIALIZED,
            session_->OperationSinglePart(kDigest, in, &len, &out));
}

TEST_F(TestSession, SinglePartOperationPreventsFinal) {
  string in(30, 'A');
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest, static_cast<int>(CKR_OK)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDigest, CKM_SHA_1, "", nullptr));
  string out;
  int len = 0;
  // Perform a single part operation but leave the output to be collected.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kDigest, in, &len, &out));

  len = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_ACTIVE)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_OPERATION_ACTIVE,
            session_->OperationFinal(kDigest, &len, &out));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_OPERATION_NOT_INITIALIZED)))
      .WillOnce(Return(true));
  // The error also terminates the operation.
  EXPECT_EQ(CKR_OPERATION_NOT_INITIALIZED,
            session_->OperationSinglePart(kDigest, in, &len, &out));
}

// Test RSA PKCS #1 encryption.
TEST_F(TestSession, RSAEncrypt) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateRSAKeyPair(false, 1024, &pub, &priv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt, static_cast<int>(CKR_OK)))
      .Times(2);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kEncrypt, CKM_RSA_PKCS, "", pub));
  string in(100, 'A');
  int len = 0;
  string out;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kEncrypt, in, &len, &out));
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kEncrypt, in, &len, &out));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDecrypt, static_cast<int>(CKR_OK)))
      .Times(3);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDecrypt, CKM_RSA_PKCS, "", priv));
  len = 0;
  string in2 = out;
  string out2;
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDecrypt, in2, &len, &out2));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDecrypt,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationFinal(kDecrypt, &len, &out2));
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kDecrypt, &len, &out2));
  EXPECT_EQ(in.length(), out2.length());
  // Check that what has been decrypted matches our original plain-text.
  EXPECT_TRUE(in == out2);
}

// Test RSA PKCS #1 sign / verify.
TEST_F(TestSession, RsaSign) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateRSAKeyPair(true, 1024, &pub, &priv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(4);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .Times(12);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionVerify, static_cast<int>(CKR_OK)))
      .Times(14);
  // Test the generic RSA mechanism.
  TestSignVerify(pub, priv, 100, CKM_RSA_PKCS, "");

  // Test RSA mechanism with built-in hash.
  TestSignVerify(pub, priv, 100, CKM_SHA256_RSA_PKCS, "");
}

// Test RSA PSS sign / verify.
TEST_F(TestSession, RsaPSSSign) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateRSAKeyPair(true, 1024, &pub, &priv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(4);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .Times(12);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionVerify, static_cast<int>(CKR_OK)))
      .Times(14);
  // Test the generic RSA PSS mechanism.
  TestSignVerify(pub, priv, 20, CKM_RSA_PKCS_PSS,
                 GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20));

  // Test the version with built-in hash.
  TestSignVerify(pub, priv, 100, CKM_SHA256_RSA_PKCS_PSS,
                 GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20));
}

// Test RSA PSS sign / verify input data length validation (b/524111614).
TEST_F(TestSession, RsaPSSSignDataLengthValidation) {
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateRSAKeyPair(true, 1024, &pub, &priv);

  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionSign, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionVerify, _))
      .WillRepeatedly(Return(true));

  int len = 0;
  string sig;

  // 1. Generic RSA PSS with SHA-1 (digest size 20)
  string pss_sha1 = GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20);
  // Truncated / empty inputs for Sign
  for (size_t bad_len : {size_t{0}, size_t{4}, size_t{19}}) {
    string bad_input(bad_len, 'A');
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha1, priv));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, bad_input, &len, &sig));
  }
  // Oversized inputs for Sign
  for (size_t bad_len : {size_t{21}, size_t{32}, size_t{64}}) {
    string bad_input(bad_len, 'B');
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha1, priv));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, bad_input, &len, &sig));
  }
  // Exact size 20 succeeds for Sign
  string valid_sha1(20, 'C');
  len = 0;
  string sig_sha1;
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha1, priv));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kSign, valid_sha1, &len, &sig_sha1));
  EXPECT_EQ(CKR_OK,
            session_->OperationSinglePart(kSign, valid_sha1, &len, &sig_sha1));

  // Verify succeeds with valid SHA-1 digest and signature
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS, pss_sha1, pub));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, valid_sha1, nullptr, nullptr));
  EXPECT_EQ(CKR_OK, session_->VerifyFinal(sig_sha1));

  // Verify rejects truncated and oversized inputs with CKR_SIGNATURE_INVALID
  for (size_t bad_len :
       {size_t{0}, size_t{4}, size_t{19}, size_t{21}, size_t{32}, size_t{64}}) {
    string bad_input(bad_len, 'A');
    EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS,
                                              pss_sha1, pub));
    EXPECT_EQ(CKR_OK,
              session_->OperationUpdate(kVerify, bad_input, nullptr, nullptr));
    EXPECT_EQ(CKR_SIGNATURE_INVALID, session_->VerifyFinal(sig_sha1));
  }

  // 2. Generic RSA PSS with SHA-256 (digest size 32)
  string pss_sha256 = GetRSAPSSParam(CKM_SHA256, CKG_MGF1_SHA256, 32);
  // Truncated inputs for Sign (reproducing b/524111614)
  for (size_t bad_len : {size_t{0}, size_t{4}, size_t{31}}) {
    string bad_input(bad_len, 'D');
    EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_RSA_PKCS_PSS,
                                              pss_sha256, priv));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, bad_input, &len, &sig));
  }
  // Oversized inputs for Sign
  for (size_t bad_len : {size_t{33}, size_t{64}}) {
    string bad_input(bad_len, 'E');
    EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_RSA_PKCS_PSS,
                                              pss_sha256, priv));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, bad_input, &len, &sig));
  }
  // Exact size 32 succeeds for Sign
  string valid_sha256(32, 'F');
  len = 0;
  string sig_sha256;
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha256, priv));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL, session_->OperationSinglePart(
                                      kSign, valid_sha256, &len, &sig_sha256));
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kSign, valid_sha256, &len,
                                                  &sig_sha256));

  // Verify succeeds with valid SHA-256 digest and signature
  EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS,
                                            pss_sha256, pub));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, valid_sha256, nullptr, nullptr));
  EXPECT_EQ(CKR_OK, session_->VerifyFinal(sig_sha256));

  // Verify rejects truncated and oversized inputs with CKR_SIGNATURE_INVALID
  for (size_t bad_len :
       {size_t{0}, size_t{4}, size_t{31}, size_t{33}, size_t{64}}) {
    string bad_input(bad_len, 'D');
    EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS,
                                              pss_sha256, pub));
    EXPECT_EQ(CKR_OK,
              session_->OperationUpdate(kVerify, bad_input, nullptr, nullptr));
    EXPECT_EQ(CKR_SIGNATURE_INVALID, session_->VerifyFinal(sig_sha256));
  }

  // 3. Multi-part signing length check
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha256, priv));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kSign, "part1", nullptr, nullptr));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kSign, "part2", nullptr, nullptr));
  // Total 10 bytes != 32 -> OperationFinal fails
  EXPECT_EQ(CKR_FUNCTION_FAILED, session_->OperationFinal(kSign, &len, &sig));

  // Multi-part signing valid case (16 bytes + 16 bytes = 32 bytes)
  len = 0;
  sig.clear();
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, pss_sha256, priv));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kSign, string(16, 'A'), nullptr,
                                              nullptr));
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kSign, string(16, 'B'), nullptr,
                                              nullptr));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL, session_->OperationFinal(kSign, &len, &sig));
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kSign, &len, &sig));

  // 4. Sentinel salt length validation (truncating to OpenSSL -1 / -2)
  for (CK_ULONG sentinel_salt :
       {static_cast<CK_ULONG>(-1), static_cast<CK_ULONG>(-2)}) {
    CK_RSA_PKCS_PSS_PARAMS sentinel_params = {CKM_SHA256, CKG_MGF1_SHA256,
                                              sentinel_salt};
    string pss_sentinel(reinterpret_cast<const char*>(&sentinel_params),
                        sizeof(sentinel_params));
    // Sign rejects sentinel salt length with CKR_FUNCTION_FAILED
    EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_RSA_PKCS_PSS,
                                              pss_sentinel, priv));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, valid_sha256, &len, &sig));

    // Verify rejects sentinel salt length with CKR_MECHANISM_PARAM_INVALID
    EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS,
                                              pss_sentinel, pub));
    EXPECT_EQ(CKR_OK, session_->OperationUpdate(kVerify, valid_sha256, nullptr,
                                                nullptr));
    EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID, session_->VerifyFinal(sig_sha256));
  }

  // 5. Key capacity bound validation (expected_size + sLen + 2 > RSA_size)
  // For 1024-bit key (128 bytes) and SHA-256 (32 bytes), sLen=100 exceeds
  // capacity: 32 + 100 + 2 = 134 > 128
  CK_RSA_PKCS_PSS_PARAMS excessive_salt_params = {CKM_SHA256, CKG_MGF1_SHA256,
                                                  100};
  string pss_excessive_salt(
      reinterpret_cast<const char*>(&excessive_salt_params),
      sizeof(excessive_salt_params));
  // Sign rejects excessive salt with CKR_FUNCTION_FAILED
  EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_RSA_PKCS_PSS,
                                            pss_excessive_salt, priv));
  EXPECT_EQ(CKR_FUNCTION_FAILED,
            session_->OperationSinglePart(kSign, valid_sha256, &len, &sig));

  // Verify rejects excessive salt with CKR_KEY_SIZE_RANGE
  EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS_PSS,
                                            pss_excessive_salt, pub));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, valid_sha256, nullptr, nullptr));
  EXPECT_EQ(CKR_KEY_SIZE_RANGE, session_->VerifyFinal(sig_sha256));
}

// Malformed RSA PSS parameters must be rejected before the request reaches the
// security element. ToHwsecSigningOptions() previously discarded the parse
// failure from GetHwsecPssParams(), so libhwsec received a kRsassaPss request
// with no PssParams and substituted a default maximal salt length and a null
// digest instead of failing the operation (b/524111614).
TEST_F(TestSession, RsaPSSHwsecRejectsMalformedParams) {
  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionSign, _))
      .WillRepeatedly(Return(true));

  // RSASign() takes the libhwsec path for any token object carrying a key
  // blob, bypassing RSASignerVerifierImplPSS entirely. The key has to live in
  // the token object pool: OperationInit() resolves it back through
  // ObjectPool::FindByHandle() to hold an owning reference for the operation.
  Object* key = CreateObjectMock();
  key->SetAttributeBool(CKA_TOKEN, true);
  key->SetAttributeInt(CKA_CLASS, CKO_PRIVATE_KEY);
  key->SetAttributeInt(CKA_KEY_TYPE, CKK_RSA);
  key->SetAttributeBool(CKA_SIGN, true);
  key->SetAttributeString(CKA_MODULUS, string(256, 'M'));
  key->SetAttributeString(kKeyBlobAttribute, "key-blob");
  key->SetAttributeString(kAuthDataAttribute, "auth-data");
  ASSERT_EQ(ObjectPool::Result::Success, token_pool_.Insert(key));

  const string digest(32, 'D');

  // Every entry makes ParseRSAPSSParams() fail. hwsec_ is a StrictMock, so
  // reaching LoadKey() or Sign() with these parameters fails the test.
  const string kMalformedParams[] = {
      // Parameter structure of the wrong size.
      GetRSAPSSParam(CKM_SHA256, CKG_MGF1_SHA256, 32).substr(1),
      // Unrecognized mask generation function.
      GetRSAPSSParam(CKM_SHA256, 0xdeadbeef, 32),
      // Salt lengths that narrow to the OpenSSL RSA_PSS_SALTLEN_DIGEST (-1)
      // and RSA_PSS_SALTLEN_AUTO (-2) sentinels when converted to int.
      GetRSAPSSParam(CKM_SHA256, CKG_MGF1_SHA256, static_cast<CK_ULONG>(-1)),
      GetRSAPSSParam(CKM_SHA256, CKG_MGF1_SHA256, static_cast<CK_ULONG>(-2)),
  };
  for (const string& params : kMalformedParams) {
    int len = 0;
    string sig;
    EXPECT_EQ(CKR_OK,
              session_->OperationInit(kSign, CKM_RSA_PKCS_PSS, params, key));
    EXPECT_EQ(CKR_FUNCTION_FAILED,
              session_->OperationSinglePart(kSign, digest, &len, &sig));
  }

  // Positive control: well-formed parameters must still reach libhwsec, so
  // that the rejections above are attributable to the parameters and not to a
  // broken hardware path. Capture the arguments rather than matching on _:
  // the point of this CL is that the *correct* options reach the security
  // element, so a regression that dropped pss_params or fell back to
  // kNoDigest has to fail this test.
  brillo::Blob signed_data;
  std::optional<hwsec::SigningOptions> signing_options;
  EXPECT_CALL(hwsec_, LoadKey(_, _)).WillOnce([this](auto&&, auto&&) {
    return GetTestScopedKey();
  });
  EXPECT_CALL(hwsec_, Sign(_, _, _))
      .WillOnce([&signed_data, &signing_options](
                    hwsec::Key, const brillo::Blob& data,
                    const hwsec::SigningOptions& options) {
        signed_data = data;
        signing_options = options;
        return brillo::Blob(256, 'S');
      });

  int len = 0;
  string sig;
  EXPECT_EQ(CKR_OK, session_->OperationInit(
                        kSign, CKM_RSA_PKCS_PSS,
                        GetRSAPSSParam(CKM_SHA256, CKG_MGF1_SHA256, 32), key));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kSign, digest, &len, &sig));
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kSign, digest, &len, &sig));

  EXPECT_EQ(brillo::BlobFromString(digest), signed_data);
  ASSERT_TRUE(signing_options.has_value());
  EXPECT_EQ(hwsec::DigestAlgorithm::kSha256, signing_options->digest_algorithm);
  ASSERT_TRUE(signing_options->rsa_padding_scheme.has_value());
  EXPECT_EQ(hwsec::SigningOptions::RsaPaddingScheme::kRsassaPss,
            signing_options->rsa_padding_scheme.value());
  ASSERT_TRUE(signing_options->pss_params.has_value());
  EXPECT_EQ(hwsec::DigestAlgorithm::kSha256,
            signing_options->pss_params->mgf1_algorithm);
  EXPECT_EQ(32u, signing_options->pss_params->salt_length);
}

// Test ECC ECDSA sign / verify.
TEST_F(TestSession, EcdsaSign) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(false, false, &pub, &priv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(4);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .Times(12);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionVerify, static_cast<int>(CKR_OK)))
      .Times(14);
  // Test the generic ECDSA.
  TestSignVerify(pub, priv, 100, CKM_ECDSA, "");

  // Test ECDSA with built-in hash.
  TestSignVerify(pub, priv, 100, CKM_ECDSA_SHA1, "");
}

// Test that requests for unsupported mechanisms are handled correctly.
TEST_F(TestSession, MechanismInvalid) {
  const Object* key = nullptr;
  // Use a valid key so that key errors don't mask mechanism errors.
  GenerateSecretKey(CKM_AES_KEY_GEN, 16, &key);
  // We don't support IDEA.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kEncrypt, CKM_IDEA_CBC, "", key));
  // We don't support SHA-224.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kSign, CKM_SHA224_RSA_PKCS, "", key));
  // We don't support MD2.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kDigest, CKM_MD2, "", nullptr));
}

// Test that operation / mechanism mismatches are handled correctly.
TEST_F(TestSession, MechanismMismatch) {
  const Object* hmac = nullptr;
  GenerateSecretKey(CKM_GENERIC_SECRET_KEY_GEN, 16, &hmac);
  const Object* aes = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 16, &aes);
  // Encrypt with a sign/verify mechanism.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kEncrypt, CKM_SHA_1_HMAC, "", hmac));
  // Sign with an encryption mechanism.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .Times(2);
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kSign, CKM_AES_CBC_PAD, "", aes));
  // Sign with a digest-only mechanism.
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kSign, CKM_SHA_1, "", hmac));
  // Digest with a sign+digest mechanism.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDigest,
                              static_cast<int>(CKR_MECHANISM_INVALID)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_MECHANISM_INVALID,
            session_->OperationInit(kDigest, CKM_SHA1_RSA_PKCS, "", nullptr));
}

// Test that mechanism / key type mismatches are handled correctly.
TEST_F(TestSession, key_typeMismatch) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* aes = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 16, &aes);
  const Object* rsapub = nullptr;
  const Object* rsapriv = nullptr;
  GenerateRSAKeyPair(true, 512, &rsapub, &rsapriv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_KEY_TYPE_INCONSISTENT)))
      .Times(2);
  // DES3 with an AES key.
  EXPECT_EQ(CKR_KEY_TYPE_INCONSISTENT,
            session_->OperationInit(kEncrypt, CKM_DES3_CBC, "", aes));
  // AES with an RSA key.
  EXPECT_EQ(CKR_KEY_TYPE_INCONSISTENT,
            session_->OperationInit(kEncrypt, CKM_AES_CBC, "", rsapriv));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_KEY_TYPE_INCONSISTENT)))
      .Times(2);
  // HMAC with an RSA key.
  EXPECT_EQ(CKR_KEY_TYPE_INCONSISTENT,
            session_->OperationInit(kSign, CKM_SHA_1_HMAC, "", rsapriv));
  // RSA with an AES key.
  EXPECT_EQ(CKR_KEY_TYPE_INCONSISTENT,
            session_->OperationInit(kSign, CKM_SHA1_RSA_PKCS, "", aes));
}

// Test that key function permissions are correctly enforced.
TEST_F(TestSession, KeyFunctionPermission) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  const Object* encpub = nullptr;
  const Object* encpriv = nullptr;
  GenerateRSAKeyPair(false, 512, &encpub, &encpriv);
  const Object* sigpub = nullptr;
  const Object* sigpriv = nullptr;
  GenerateRSAKeyPair(true, 512, &sigpub, &sigpriv);
  // Try decrypting with a sign-only key.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDecrypt,
                              static_cast<int>(CKR_KEY_FUNCTION_NOT_PERMITTED)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_KEY_FUNCTION_NOT_PERMITTED,
            session_->OperationInit(kDecrypt, CKM_RSA_PKCS, "", sigpriv));
  // Try signing with a decrypt-only key.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign,
                              static_cast<int>(CKR_KEY_FUNCTION_NOT_PERMITTED)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_KEY_FUNCTION_NOT_PERMITTED,
            session_->OperationInit(kSign, CKM_RSA_PKCS, "", encpriv));
}

// Test that invalid mechanism parameters for ciphers are handled correctly.
TEST_F(TestSession, BadIV) {
  const Object* aes = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 16, &aes);
  const Object* des = nullptr;
  GenerateSecretKey(CKM_DES_KEY_GEN, 16, &des);
  const Object* des3 = nullptr;
  GenerateSecretKey(CKM_DES3_KEY_GEN, 16, &des3);
  // AES expects 16 bytes and DES/DES3 expects 8 bytes.
  string bad_iv(7, 0);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_MECHANISM_PARAM_INVALID)))
      .Times(3);
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->OperationInit(kEncrypt, CKM_AES_CBC, bad_iv, aes));
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->OperationInit(kEncrypt, CKM_DES_CBC, bad_iv, des));
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->OperationInit(kEncrypt, CKM_DES3_CBC, bad_iv, des3));
}

// Test that invalid key size ranges are handled correctly.
TEST_F(TestSession, BadKeySize) {
  const Object* key = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 16, &key);
  // AES keys can be 16, 24, or 32 bytes in length.
  const_cast<Object*>(key)->SetAttributeString(CKA_VALUE, string(33, 0));
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionEncrypt,
                              static_cast<int>(CKR_KEY_SIZE_RANGE)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_KEY_SIZE_RANGE,
            session_->OperationInit(kEncrypt, CKM_AES_ECB, "", key));
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateRSAKeyPair(true, 512, &pub, &priv);
  // RSA keys can have a modulus size no smaller than 512.
  const_cast<Object*>(priv)->SetAttributeString(CKA_MODULUS, string(32, 0));
  EXPECT_CALL(
      mock_metrics_library_,
      SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_KEY_SIZE_RANGE)))
      .WillOnce(Return(true));
  EXPECT_EQ(CKR_KEY_SIZE_RANGE,
            session_->OperationInit(kSign, CKM_RSA_PKCS, "", priv));
}

// Test that invalid attributes for key pair generation are handled correctly.
TEST_F(TestSession, BadRSAGenerate) {
  CK_BBOOL no = CK_FALSE;
  int size = 1024;
  CK_BYTE pubexp[] = {1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 12},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {
      {CKA_TOKEN, &no, sizeof(no)},
  };
  int pub, priv;
  // CKA_PUBLIC_EXPONENT too large.
  EXPECT_EQ(CKR_FUNCTION_FAILED,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pub, &priv));
  pub_attr[1].ulValueLen = 3;
  size = 20000;
  // CKA_MODULUS_BITS too large.
  EXPECT_EQ(CKR_KEY_SIZE_RANGE,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pub, &priv));
  // CKA_MODULUS_BITS missing.
  EXPECT_EQ(
      CKR_TEMPLATE_INCOMPLETE,
      session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr, 2,
                                priv_attr, std::size(priv_attr), &pub, &priv));
}

// Test that invalid attributes for key generation are handled correctly.
TEST_F(TestSession, BadAESGenerate) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  int size = 33;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_ENCRYPT, &yes, sizeof(yes)},
                         {CKA_DECRYPT, &yes, sizeof(yes)},
                         {CKA_VALUE_LEN, &size, sizeof(size)}};
  int handle = 0;
  // CKA_VALUE_LEN missing.
  EXPECT_EQ(CKR_TEMPLATE_INCOMPLETE,
            session_->GenerateKey(CKM_AES_KEY_GEN, "", attr, 3, &handle));
  // CKA_VALUE_LEN out of range.
  EXPECT_EQ(CKR_KEY_SIZE_RANGE,
            session_->GenerateKey(CKM_AES_KEY_GEN, "", attr, 4, &handle));
}

// Test that signature verification fails as expected for invalid signatures.
TEST_F(TestSession, BadSignature) {
  string input(100, 'A');
  string signature(20, 0);
  const Object* hmac;
  GenerateSecretKey(CKM_GENERIC_SECRET_KEY_GEN, 32, &hmac);
  const Object *rsapub, *rsapriv;
  GenerateRSAKeyPair(true, 1024, &rsapub, &rsapriv);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionVerify, static_cast<int>(CKR_OK)))
      .Times(12);
  // HMAC with bad signature length.
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kVerify, CKM_SHA256_HMAC, "", hmac));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, input, nullptr, nullptr));
  EXPECT_EQ(CKR_SIGNATURE_LEN_RANGE, session_->VerifyFinal(signature));
  // HMAC with bad signature.
  signature.resize(32);
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kVerify, CKM_SHA256_HMAC, "", hmac));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, input, nullptr, nullptr));
  EXPECT_EQ(CKR_SIGNATURE_INVALID, session_->VerifyFinal(signature));
  // RSA with bad signature length.
  EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS, "", rsapub));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, input, nullptr, nullptr));
  EXPECT_EQ(CKR_SIGNATURE_LEN_RANGE, session_->VerifyFinal(signature));
  // RSA with bad signature.
  signature.resize(128, 1);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kVerify, CKM_RSA_PKCS, "", rsapub));
  EXPECT_EQ(CKR_OK,
            session_->OperationUpdate(kVerify, input, nullptr, nullptr));
  EXPECT_EQ(CKR_SIGNATURE_INVALID, session_->VerifyFinal(signature));
}

TEST_F(TestSession, Flush) {
  ObjectMock token_object;
  EXPECT_CALL(token_object, IsTokenObject()).WillRepeatedly(Return(true));
  ObjectMock session_object;
  EXPECT_CALL(session_object, IsTokenObject()).WillRepeatedly(Return(false));
  EXPECT_CALL(token_pool_, Flush(_))
      .WillOnce(Return(Result::Failure))
      .WillRepeatedly(Return(Result::Success));
  EXPECT_NE(session_->FlushModifiableObject(&token_object), CKR_OK);
  EXPECT_EQ(session_->FlushModifiableObject(&token_object), CKR_OK);
  EXPECT_EQ(session_->FlushModifiableObject(&session_object), CKR_OK);
}

TEST_F(TestSessionWithRealObject, GenerateRSAWithHWSec) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(
      hwsec_,
      GenerateRSAKey(_, _, _, hwsec::ChapsFrontend::AllowSoftwareGen::kNotAllow,
                     hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                     hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetRSAPublicKey(_))
      .WillRepeatedly(ReturnValue(hwsec::RSAPublicInfo{}));

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  // There are a few sensitive attributes that MUST not exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &object));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Check attributes that store security element wrapped blob exists.
  EXPECT_TRUE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_TRUE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_FALSE(object->GetAttributeBool(kKeyInSoftwareAttribute, true));
}

TEST_F(TestSessionWithRealObject, GenerateRSAWithHWSecAndAllowSoftGen) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(
      hwsec_,
      GenerateRSAKey(_, _, _, hwsec::ChapsFrontend::AllowSoftwareGen::kAllow,
                     hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                     hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetRSAPublicKey(_))
      .WillRepeatedly(ReturnValue(hwsec::RSAPublicInfo{}));

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)},
                              {kAllowSoftwareGenAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  // There are a few sensitive attributes that MUST not exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &object));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Check attributes that store security element wrapped blob exists.
  EXPECT_TRUE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_TRUE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_FALSE(object->GetAttributeBool(kKeyInSoftwareAttribute, true));
}

TEST_F(TestSessionWithRealObject, GenerateRSAWithHWsecInconsistentToken) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(
      hwsec_,
      GenerateRSAKey(_, _, _, _, hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                     hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetRSAPublicKey(_))
      .WillRepeatedly(ReturnValue(hwsec::RSAPublicInfo{}));

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)}};
  // Attempt to generate a private key on the token, but public key not on the
  // token.
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  const Object* public_object = nullptr;
  const Object* private_object = nullptr;
  ASSERT_TRUE(session_->GetObject(pubh, &public_object));
  ASSERT_TRUE(session_->GetObject(privh, &private_object));
  EXPECT_FALSE(public_object->IsTokenObject());
  EXPECT_TRUE(private_object->IsTokenObject());

  // Destroy the objects.
  EXPECT_EQ(CKR_OK, session_->DestroyObject(pubh));
  EXPECT_EQ(CKR_OK, session_->DestroyObject(privh));
}

TEST_F(TestSessionWithRealObject, GenerateRSAWithNoHWSec) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(
          ReturnError<TPMError>("Not supported", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));
  EXPECT_CALL(hwsec_, GenerateRSAKey(_, _, _, _, _, _)).Times(0);

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int size = 1024;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  // For a software key, the sensitive attributes should exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));

  // Check attributes that store security element wrapped blob doesn't exists.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));
}

TEST_F(TestSessionWithRealObject, GenerateRSAWithForceSoftware) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, GenerateRSAKey(_, _, _, _, _, _)).Times(0);

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int size = 1024;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  // For a software key, the sensitive attributes should exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));

  // Check attributes that store security element wrapped blob doesn't exists.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));
}

TEST_F(TestSessionWithRealObject, GenerateECCWithHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, GenerateECCKey(
                          _, _, hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                          hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetECCPublicKey(_))
      .WillRepeatedly(ReturnValue(GenerateECCPublicInfo()));

  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(true, true, &pub, &priv);

  // Security element backed key object doesn't have CKA_VALUE which stored ECC
  // private key.
  EXPECT_FALSE(priv->IsAttributePresent(CKA_VALUE));

  // Check attributes that store security element wrapped blob exists.
  EXPECT_TRUE(priv->IsAttributePresent(kAuthDataAttribute));
  EXPECT_TRUE(priv->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(priv->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_FALSE(priv->GetAttributeBool(kKeyInSoftwareAttribute, true));
}

TEST_F(TestSessionWithRealObject, GenerateECCWithHWSecInconsistentToken) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, GenerateECCKey(
                          _, _, hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                          hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetECCPublicKey(_))
      .WillRepeatedly(ReturnValue(GenerateECCPublicInfo()));

  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(false, true, &pub, &priv);

  EXPECT_FALSE(pub->IsTokenObject());
  EXPECT_TRUE(priv->IsTokenObject());
}

TEST_F(TestSessionWithRealObject, GenerateECCWithNoHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(
          ReturnError<TPMError>("Not supported", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));

  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(true, true, &pub, &priv);

  // For a software key, the sensitive attributes should exist.
  EXPECT_TRUE(priv->IsAttributePresent(CKA_VALUE));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(priv->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(priv->GetAttributeBool(kKeyInSoftwareAttribute, false));

  // Check attributes that store security element wrapped blob doesn't exists.
  EXPECT_FALSE(priv->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(priv->IsAttributePresent(kKeyBlobAttribute));
}

TEST_F(TestSessionWithRealObject, GenerateECCWithForceSoftware) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));

  const Object* priv = nullptr;

  // Create DER encoded OID of P-256 for CKA_EC_PARAMS (prime256v1 or
  // secp256r1)
  string ec_params = GetDERforNID(NID_X9_62_prime256v1);

  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_ATTRIBUTE pub_attr[] = {
      {CKA_TOKEN, &yes, sizeof(CK_BBOOL)},
      {CKA_ENCRYPT, &no, sizeof(CK_BBOOL)},
      {CKA_VERIFY, &yes, sizeof(CK_BBOOL)},
      {CKA_EC_PARAMS, ConvertStringToByteBuffer(ec_params.data()),
       ec_params.size()}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(CK_BBOOL)},
                              {CKA_DECRYPT, &no, sizeof(CK_BBOOL)},
                              {CKA_SIGN, &yes, sizeof(CK_BBOOL)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK, session_->GenerateKeyPair(
                        CKM_EC_KEY_PAIR_GEN, "", pub_attr, std::size(pub_attr),
                        priv_attr, std::size(priv_attr), &pubh, &privh));
  ASSERT_TRUE(session_->GetObject(privh, &priv));

  // For a software key, the sensitive attributes should exist.
  EXPECT_TRUE(priv->IsAttributePresent(CKA_VALUE));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(priv->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(priv->GetAttributeBool(kKeyInSoftwareAttribute, false));

  // Check attributes that store security element wrapped blob doesn't exists.
  EXPECT_FALSE(priv->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(priv->IsAttributePresent(kKeyBlobAttribute));
}

TEST_F(TestSession, EcdsaSignWithHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, GenerateECCKey(
                          _, _, hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                          hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });
  EXPECT_CALL(hwsec_, GetECCPublicKey(_))
      .WillRepeatedly(ReturnValue(GenerateECCPublicInfo()));
  EXPECT_CALL(hwsec_, LoadKey(_, _)).WillRepeatedly([&](auto&&, auto&&) {
    return GetTestScopedKey();
  });
  EXPECT_CALL(hwsec_, Sign(_, _, _))
      .WillRepeatedly(ReturnValue(brillo::Blob()));

  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(true, true, &pub, &priv);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .Times(2);
  EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_ECDSA_SHA1, "", priv));
  string in(100, 'A');
  int len = 0;
  string sig;
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kSign, in, &len, &sig));
}

TEST_F(TestSessionWithRealObject, ImportRSAWithHWSec) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_,
              WrapRSAKey(_, _, _, _, hwsec::ChapsFrontend::AllowDecrypt::kAllow,
                         hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });

  crypto::ScopedBIGNUM exponent(BN_new());
  CHECK(exponent);
  EXPECT_TRUE(BN_set_word(exponent.get(), 0x10001));
  crypto::ScopedRSA rsa(RSA_new());
  CHECK(rsa);
  EXPECT_TRUE(RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr));
  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_RSA;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  const BIGNUM* rsa_n;
  const BIGNUM* rsa_e;
  const BIGNUM* rsa_d;
  const BIGNUM* rsa_p;
  const BIGNUM* rsa_q;
  const BIGNUM* rsa_dmp1;
  const BIGNUM* rsa_dmq1;
  const BIGNUM* rsa_iqmp;
  RSA_get0_key(rsa.get(), &rsa_n, &rsa_e, &rsa_d);
  RSA_get0_factors(rsa.get(), &rsa_p, &rsa_q);
  RSA_get0_crt_params(rsa.get(), &rsa_dmp1, &rsa_dmq1, &rsa_iqmp);
  string n = bn2bin(rsa_n);
  string e = bn2bin(rsa_e);
  string d = bn2bin(rsa_d);
  string p = bn2bin(rsa_p);
  string q = bn2bin(rsa_q);
  string dmp1 = bn2bin(rsa_dmp1);
  string dmq1 = bn2bin(rsa_dmq1);
  string iqmp = bn2bin(rsa_iqmp);

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_MODULUS, std::data(n), n.length()},
      {CKA_PUBLIC_EXPONENT, std::data(e), e.length()},
      {CKA_PRIVATE_EXPONENT, std::data(d), d.length()},
      {CKA_PRIME_1, std::data(p), p.length()},
      {CKA_PRIME_2, std::data(q), q.length()},
      {CKA_EXPONENT_1, std::data(dmp1), dmp1.length()},
      {CKA_EXPONENT_2, std::data(dmq1), dmq1.length()},
      {CKA_COEFFICIENT, std::data(iqmp), iqmp.length()},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));
  // There are a few sensitive attributes that MUST be removed.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_FALSE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_FALSE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Check attributes that store security element wrapped blob exists.
  EXPECT_TRUE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_TRUE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_FALSE(object->GetAttributeBool(kKeyInSoftwareAttribute, true));
}

TEST_F(TestSessionWithRealObject, ImportRSAWithNoHWSec) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(
          ReturnError<TPMError>("Not supported", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));

  crypto::ScopedBIGNUM exponent(BN_new());
  CHECK(exponent);
  EXPECT_TRUE(BN_set_word(exponent.get(), 0x10001));
  crypto::ScopedRSA rsa(RSA_new());
  CHECK(rsa);
  EXPECT_TRUE(RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr));
  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_RSA;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  const BIGNUM* rsa_n;
  const BIGNUM* rsa_e;
  const BIGNUM* rsa_d;
  const BIGNUM* rsa_p;
  const BIGNUM* rsa_q;
  const BIGNUM* rsa_dmp1;
  const BIGNUM* rsa_dmq1;
  const BIGNUM* rsa_iqmp;
  RSA_get0_key(rsa.get(), &rsa_n, &rsa_e, &rsa_d);
  RSA_get0_factors(rsa.get(), &rsa_p, &rsa_q);
  RSA_get0_crt_params(rsa.get(), &rsa_dmp1, &rsa_dmq1, &rsa_iqmp);
  string n = bn2bin(rsa_n);
  string e = bn2bin(rsa_e);
  string d = bn2bin(rsa_d);
  string p = bn2bin(rsa_p);
  string q = bn2bin(rsa_q);
  string dmp1 = bn2bin(rsa_dmp1);
  string dmq1 = bn2bin(rsa_dmq1);
  string iqmp = bn2bin(rsa_iqmp);

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_MODULUS, std::data(n), n.length()},
      {CKA_PUBLIC_EXPONENT, std::data(e), e.length()},
      {CKA_PRIVATE_EXPONENT, std::data(d), d.length()},
      {CKA_PRIME_1, std::data(p), p.length()},
      {CKA_PRIME_2, std::data(q), q.length()},
      {CKA_EXPONENT_1, std::data(dmp1), dmp1.length()},
      {CKA_EXPONENT_2, std::data(dmq1), dmq1.length()},
      {CKA_COEFFICIENT, std::data(iqmp), iqmp.length()},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));
  // For a software key, the sensitive attributes should still exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Software key should not have security element related attributes.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));
}

TEST_F(TestSessionWithRealObject, ImportRSAWithForceSoftware) {
  EXPECT_CALL(hwsec_, IsRSAModulusSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, WrapRSAKey(_, _, _, _, _, _)).Times(0);

  crypto::ScopedBIGNUM exponent(BN_new());
  CHECK(exponent);
  EXPECT_TRUE(BN_set_word(exponent.get(), 0x10001));
  crypto::ScopedRSA rsa(RSA_new());
  CHECK(rsa);
  EXPECT_TRUE(RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr));
  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_RSA;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  const BIGNUM* rsa_n;
  const BIGNUM* rsa_e;
  const BIGNUM* rsa_d;
  const BIGNUM* rsa_p;
  const BIGNUM* rsa_q;
  const BIGNUM* rsa_dmp1;
  const BIGNUM* rsa_dmq1;
  const BIGNUM* rsa_iqmp;
  RSA_get0_key(rsa.get(), &rsa_n, &rsa_e, &rsa_d);
  RSA_get0_factors(rsa.get(), &rsa_p, &rsa_q);
  RSA_get0_crt_params(rsa.get(), &rsa_dmp1, &rsa_dmq1, &rsa_iqmp);
  string n = bn2bin(rsa_n);
  string e = bn2bin(rsa_e);
  string d = bn2bin(rsa_d);
  string p = bn2bin(rsa_p);
  string q = bn2bin(rsa_q);
  string dmp1 = bn2bin(rsa_dmp1);
  string dmq1 = bn2bin(rsa_dmq1);
  string iqmp = bn2bin(rsa_iqmp);

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_MODULUS, std::data(n), n.length()},
      {CKA_PUBLIC_EXPONENT, std::data(e), e.length()},
      {CKA_PRIVATE_EXPONENT, std::data(d), d.length()},
      {CKA_PRIME_1, std::data(p), p.length()},
      {CKA_PRIME_2, std::data(q), q.length()},
      {CKA_EXPONENT_1, std::data(dmp1), dmp1.length()},
      {CKA_EXPONENT_2, std::data(dmq1), dmq1.length()},
      {CKA_COEFFICIENT, std::data(iqmp), iqmp.length()},
      {kForceSoftwareAttribute, &true_value, sizeof(true_value)},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));
  // For a software key, the sensitive attributes should still exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIVATE_EXPONENT));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_PRIME_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_1));
  EXPECT_TRUE(object->IsAttributePresent(CKA_EXPONENT_2));
  EXPECT_TRUE(object->IsAttributePresent(CKA_COEFFICIENT));

  // Software key should not have security element related attributes.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));
}

TEST_F(TestSessionWithRealObject, ImportECCWithHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(NID_X9_62_prime256v1))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, WrapECCKey(_, _, _, _, _,
                                 hwsec::ChapsFrontend::AllowDecrypt::kAllow,
                                 hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });

  crypto::ScopedEC_KEY key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
  ASSERT_NE(key, nullptr);
  // Focus GetECParametersAsString() dump OID to CKA_EC_PARAMS
  EC_KEY_set_asn1_flag(key.get(), OPENSSL_EC_NAMED_CURVE);
  ASSERT_TRUE(EC_KEY_generate_key(key.get()));

  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_EC;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  string ec_params = GetECParametersAsString(key.get());
  string private_value = bn2bin(EC_KEY_get0_private_key(key.get()));

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_EC_PARAMS, std::data(ec_params), ec_params.length()},
      {CKA_VALUE, std::data(private_value), private_value.length()},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));

  // There are a few sensitive attributes that MUST be removed.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_FALSE(object->IsAttributePresent(CKA_VALUE));

  // Check attributes that store security element wrapped blob exists.
  EXPECT_TRUE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_TRUE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_FALSE(object->GetAttributeBool(kKeyInSoftwareAttribute, true));
}

TEST_F(TestSessionWithRealObject, ImportECCWithNoHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(
          ReturnError<TPMError>("Not supported", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(false));

  crypto::ScopedEC_KEY key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
  ASSERT_NE(key, nullptr);
  ASSERT_TRUE(EC_KEY_generate_key(key.get()));

  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_EC;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  string ec_params = GetECParametersAsString(key.get());
  string private_value = bn2bin(EC_KEY_get0_private_key(key.get()));

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_EC_PARAMS, std::data(ec_params), ec_params.length()},
      {CKA_VALUE, std::data(private_value), private_value.length()},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));

  // For a software key, the sensitive attributes should still exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_VALUE));

  // Software key should not have security element related attributes.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));
}

TEST_F(TestSessionWithRealObject, ImportECCWithForceSoftware) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(NID_X9_62_prime256v1))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, WrapECCKey(_, _, _, _, _, _, _)).Times(0);

  crypto::ScopedEC_KEY key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
  ASSERT_NE(key, nullptr);
  ASSERT_TRUE(EC_KEY_generate_key(key.get()));

  CK_OBJECT_CLASS priv_class = CKO_PRIVATE_KEY;
  CK_KEY_TYPE key_type = CKK_EC;
  CK_BBOOL false_value = CK_FALSE;
  CK_BBOOL true_value = CK_TRUE;
  string id = "test_id";
  string label = "test_label";
  string ec_params = GetECParametersAsString(key.get());
  string private_value = bn2bin(EC_KEY_get0_private_key(key.get()));

  CK_ATTRIBUTE private_attributes[] = {
      {CKA_CLASS, &priv_class, sizeof(priv_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_DECRYPT, &true_value, sizeof(true_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_ID, std::data(id), id.length()},
      {CKA_LABEL, std::data(label), label.length()},
      {CKA_EC_PARAMS, std::data(ec_params), ec_params.length()},
      {CKA_VALUE, std::data(private_value), private_value.length()},
      {kForceSoftwareAttribute, &true_value, sizeof(true_value)},
  };

  int handle = 0;
  ASSERT_EQ(CKR_OK,
            session_->CreateObject(private_attributes,
                                   std::size(private_attributes), &handle));

  // For a software key, the sensitive attributes should still exist.
  const Object* object = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &object));
  EXPECT_TRUE(object->IsAttributePresent(CKA_VALUE));

  // Software key should not have security element related attributes.
  EXPECT_FALSE(object->IsAttributePresent(kAuthDataAttribute));
  EXPECT_FALSE(object->IsAttributePresent(kKeyBlobAttribute));

  // Check that kKeyInSoftwareAttribute attribute is correctly set.
  EXPECT_TRUE(object->IsAttributePresent(kKeyInSoftwareAttribute));
  EXPECT_TRUE(object->GetAttributeBool(kKeyInSoftwareAttribute, false));
}

TEST_F(TestSessionWithRealObject, DeriveKeySp800108) {
  const Object* base_key;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &base_key);
  const_cast<Object*>(base_key)->SetAttributeBool(CKA_DERIVE, true);
  ASSERT_TRUE(base_key->GetAttributeBool(CKA_NEVER_EXTRACTABLE, true));
  ASSERT_TRUE(base_key->GetAttributeBool(CKA_ALWAYS_SENSITIVE, true));

  CK_BYTE label[] = {0xde, 0xad, 0xbe, 0xef};
  CK_BYTE context[] = {0xfe, 0xed, 0xbe, 0xef};
  CK_PRF_DATA_PARAM data_params[]{
      {CK_SP800_108_BYTE_ARRAY, label, sizeof(label)},
      {CK_SP800_108_BYTE_ARRAY, context, sizeof(context)},
  };
  CK_SP800_108_KDF_PARAMS kdf_params = {CKM_SHA256_HMAC, std::size(data_params),
                                        data_params, 0, nullptr};
  Sp800108KdfParams kdf_params_proto = Sp800108KdfParamsToProto(&kdf_params);
  CK_OBJECT_CLASS key_class = CKO_SECRET_KEY;
  CK_KEY_TYPE key_type = CKK_AES;
  CK_BBOOL yes = CK_TRUE;
  CK_ULONG size = 32;
  CK_ATTRIBUTE derived_key_attr[] = {
      {CKA_CLASS, &key_class, sizeof(key_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ENCRYPT, &yes, sizeof(yes)},
      {CKA_VALUE_LEN, &size, sizeof(size)}};
  int handle = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDeriveKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_EQ(CKR_OK, session_->DeriveKey(CKM_SP800_108_COUNTER_KDF,
                                        kdf_params_proto.SerializeAsString(),
                                        *base_key, derived_key_attr,
                                        std::size(derived_key_attr), &handle));
  const Object* derived_key;
  ASSERT_TRUE(session_->GetObject(handle, &derived_key));
  EXPECT_TRUE(derived_key->GetAttributeInt(CKA_KEY_GEN_MECHANISM, 0) ==
              CK_UNAVAILABLE_INFORMATION);
  EXPECT_TRUE(derived_key->GetAttributeBool(CKA_NEVER_EXTRACTABLE, true));
  EXPECT_TRUE(derived_key->GetAttributeBool(CKA_ALWAYS_SENSITIVE, true));
}

TEST_F(TestSessionWithRealObject, DeriveKeySp800108InvalidKdfParams) {
  const Object* base_key;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &base_key);
  const_cast<Object*>(base_key)->SetAttributeBool(CKA_DERIVE, true);

  CK_BBOOL yes = CK_TRUE;
  CK_ULONG size = 32;
  CK_OBJECT_CLASS key_class = CKO_SECRET_KEY;
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE derived_key_attr[] = {
      {CKA_CLASS, &key_class, sizeof(key_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ENCRYPT, &yes, sizeof(yes)},
      {CKA_VALUE_LEN, &size, sizeof(size)}};

  // The mechanism_parameter isn't serialized from a Sp800108KdfParams proto.
  int handle = 0;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDeriveKey,
                              static_cast<int>(CKR_MECHANISM_PARAM_INVALID)))
      .Times(3);
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "test", *base_key,
                                derived_key_attr, std::size(derived_key_attr),
                                &handle));

  // An empty mechanism_parameter can be deserialized into a Sp800108KdfParams
  // proto, but it'll fail later when parsing label and context from
  // CK_PRF_DATA_PARAM.
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "", *base_key,
                                derived_key_attr, std::size(derived_key_attr),
                                &handle));

  // The CK_PRF_DATA_PARAM obeys our implementation assumption (having only two
  // entries, representing the label, and context in order).
  CK_SP800_108_COUNTER_FORMAT counter_format = {0, 16};
  CK_SP800_108_DKM_LENGTH_FORMAT dkm_format = {
      CK_SP800_108_DKM_LENGTH_SUM_OF_KEYS, 0, 16};
  CK_PRF_DATA_PARAM data_params[] = {
      {CK_SP800_108_ITERATION_VARIABLE, &counter_format,
       sizeof(counter_format)},
      {CK_SP800_108_DKM_LENGTH, &dkm_format, sizeof(dkm_format)}};
  CK_SP800_108_KDF_PARAMS kdf_params = {CKM_SHA256_HMAC, std::size(data_params),
                                        data_params, 0, nullptr};
  Sp800108KdfParams kdf_params_proto = Sp800108KdfParamsToProto(&kdf_params);
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "test", *base_key,
                                derived_key_attr, std::size(derived_key_attr),
                                &handle));
}

TEST_F(TestSessionWithRealObject, DeriveKeySp800108InvalidAttributes) {
  CK_BBOOL yes = CK_TRUE;
  CK_ULONG size = 32;
  int handle = 0;
  const Object* base_key;

  // Generate the base_key for encrypt/decrypt.
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &base_key);
  CK_OBJECT_CLASS key_class = CKO_SECRET_KEY;
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE derived_key_attr[] = {
      {CKA_CLASS, &key_class, sizeof(key_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ENCRYPT, &yes, sizeof(yes)},
      {CKA_VALUE_LEN, &size, sizeof(size)}};

  // The base_key doesn't have CKA_DERIVE attribute set to CK_TRUE.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDeriveKey,
                              static_cast<int>(CKR_KEY_FUNCTION_NOT_PERMITTED)))
      .Times(1);
  EXPECT_EQ(CKR_KEY_FUNCTION_NOT_PERMITTED,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "test", *base_key,
                                derived_key_attr, std::size(derived_key_attr),
                                &handle));

  // Set the base_key for derive.
  const_cast<Object*>(base_key)->SetAttributeBool(CKA_DERIVE, true);

  key_type = CKK_DES;
  CK_ATTRIBUTE derived_key_attr2[] = {
      {CKA_CLASS, &key_class, sizeof(key_class)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ENCRYPT, &yes, sizeof(yes)},
      {CKA_VALUE_LEN, &size, sizeof(size)}};
  // Currently we don't support deriving DES keys using SP800-108.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDeriveKey,
                              static_cast<int>(CKR_FUNCTION_NOT_SUPPORTED)))
      .Times(1);
  EXPECT_EQ(CKR_FUNCTION_NOT_SUPPORTED,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "test", *base_key,
                                derived_key_attr2, std::size(derived_key_attr2),
                                &handle));

  key_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE rsapriv_key_attr[] = {{CKA_CLASS, &key_class, sizeof(key_class)},
                                     {CKA_ENCRYPT, &yes, sizeof(yes)},
                                     {CKA_VALUE_LEN, &size, sizeof(size)}};
  // We cannot only derive symmetric keys using SP800-108.
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionDeriveKey,
                              static_cast<int>(CKR_TEMPLATE_INCONSISTENT)))
      .Times(1);
  EXPECT_EQ(CKR_TEMPLATE_INCONSISTENT,
            session_->DeriveKey(CKM_SP800_108_COUNTER_KDF, "test", *base_key,
                                rsapriv_key_attr, std::size(rsapriv_key_attr),
                                &handle));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPSoftware) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  int handle = 0;
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const Object* aeskey_new = nullptr;
  session_->GetObject(handle, &aeskey_new);
  EXPECT_EQ(aes_size, aeskey_new->GetAttributeInt(CKA_VALUE_LEN, 0));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_LOCAL, true));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_ALWAYS_SENSITIVE, true));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_NEVER_EXTRACTABLE, true));
  EXPECT_EQ(true, aeskey_new->GetAttributeBool(CKA_EXTRACTABLE, false));
  EXPECT_EQ(aeskey->GetAttributeString(CKA_VALUE),
            aeskey_new->GetAttributeString(CKA_VALUE));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPMismatchedValueLen) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  int inflated_value_len = 100;
  const_cast<Object*>(aeskey)->SetAttributeInt(CKA_VALUE_LEN,
                                               inflated_value_len);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  int handle = 0;
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const Object* aeskey_new = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &aeskey_new));
  EXPECT_EQ(aes_size, aeskey_new->GetAttributeInt(CKA_VALUE_LEN, 0));
  EXPECT_EQ(aeskey->GetAttributeString(CKA_VALUE),
            aeskey_new->GetAttributeString(CKA_VALUE));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPDeflatedValueLen) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  int deflated_value_len = 16;
  const_cast<Object*>(aeskey)->SetAttributeInt(CKA_VALUE_LEN,
                                               deflated_value_len);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  int handle = 0;
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const Object* aeskey_new = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &aeskey_new));
  EXPECT_EQ(aes_size, aeskey_new->GetAttributeInt(CKA_VALUE_LEN, 0));
  EXPECT_EQ(aeskey->GetAttributeString(CKA_VALUE),
            aeskey_new->GetAttributeString(CKA_VALUE));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPEmptyKey) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapub = nullptr;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));

  const Object* aeskey = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  const_cast<Object*>(aeskey)->SetAttributeString(CKA_VALUE, "");

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_SIZE_RANGE)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_KEY_SIZE_RANGE, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub,
                                                  *aeskey, &len, &wrapped_key));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPOversizedPayload) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapub = nullptr;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));

  const Object* aeskey = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  string oversized(250, 'A');
  const_cast<Object*>(aeskey)->SetAttributeString(CKA_VALUE, oversized);
  const_cast<Object*>(aeskey)->SetAttributeInt(CKA_VALUE_LEN, oversized.size());

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_SIZE_RANGE)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_KEY_SIZE_RANGE, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub,
                                                  *aeskey, &len, &wrapped_key));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPBoundaryCapacity) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  // Max OAEP payload for 2048-bit RSA with SHA-1: 256 - 2*20 - 2 = 214 bytes.
  string boundary(214, 'B');
  const_cast<Object*>(aeskey)->SetAttributeString(CKA_VALUE, boundary);
  const_cast<Object*>(aeskey)->SetAttributeInt(CKA_VALUE_LEN, boundary.size());

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));
  CK_KEY_TYPE key_type = CKK_GENERIC_SECRET;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  int handle = 0;
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const Object* aeskey_new = nullptr;
  ASSERT_TRUE(session_->GetObject(handle, &aeskey_new));
  EXPECT_EQ(boundary.size(), aeskey_new->GetAttributeInt(CKA_VALUE_LEN, 0));
  EXPECT_EQ(boundary, aeskey_new->GetAttributeString(CKA_VALUE));
}

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPStrictOffByOne) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapub = nullptr;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));

  const Object* aeskey = nullptr;
  GenerateSecretKey(CKM_AES_KEY_GEN, 32, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);
  // Boundary + 1: 215 bytes on 2048-bit RSA must be rejected with
  // CKR_KEY_SIZE_RANGE.
  string off_by_one(215, 'C');
  const_cast<Object*>(aeskey)->SetAttributeString(CKA_VALUE, off_by_one);
  const_cast<Object*>(aeskey)->SetAttributeInt(CKA_VALUE_LEN,
                                               off_by_one.size());

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_SIZE_RANGE)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_KEY_SIZE_RANGE, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub,
                                                  *aeskey, &len, &wrapped_key));
}

TEST_F(TestSessionWithRealObject, UnwrapKeyRSAOAEPInvalidCiphertextLen) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapriv = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_WRAPPED_KEY_LEN_RANGE)))
      .Times(2);

  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_KEY_TYPE, &key_type, sizeof(key_type)}};
  int handle = 0;

  // 1. Truncated ciphertext (100 bytes instead of 256 bytes).
  string truncated_ciphertext(100, 'X');
  EXPECT_EQ(
      CKR_WRAPPED_KEY_LEN_RANGE,
      session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, truncated_ciphertext,
                          attr, std::size(attr), &handle));

  // 2. Oversized ciphertext (300 bytes instead of 256 bytes).
  string oversized_ciphertext(300, 'Y');
  EXPECT_EQ(
      CKR_WRAPPED_KEY_LEN_RANGE,
      session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, oversized_ciphertext,
                          attr, std::size(attr), &handle));
}

TEST_F(TestSessionWithRealObject,
       UnwrapKeyRSAOAEPTemplateInconsistentValueLen) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_TEMPLATE_INCONSISTENT)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  ASSERT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  ASSERT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));

  // Inconsistent CKA_VALUE_LEN in template (16 instead of actual 32).
  int bad_value_len = 16;
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {
      {CKA_TOKEN, &no, sizeof(no)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_VALUE_LEN, &bad_value_len, sizeof(bad_value_len)}};
  int handle = 0;
  EXPECT_EQ(CKR_TEMPLATE_INCONSISTENT,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
}

#if USE_TPM2
TEST_F(TestSessionWithTpmSimulator,
       UnwrapKeyRSAOAEPInvalidCiphertextLenWithHWSec) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapriv = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_WRAPPED_KEY_LEN_RANGE)))
      .Times(2);

  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_KEY_TYPE, &key_type, sizeof(key_type)}};
  int handle = 0;

  // 1. Truncated ciphertext (100 bytes instead of 256 bytes).
  string truncated_ciphertext(100, 'X');
  EXPECT_EQ(
      CKR_WRAPPED_KEY_LEN_RANGE,
      session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, truncated_ciphertext,
                          attr, std::size(attr), &handle));

  // 2. Oversized ciphertext (300 bytes instead of 256 bytes).
  string oversized_ciphertext(300, 'Y');
  EXPECT_EQ(
      CKR_WRAPPED_KEY_LEN_RANGE,
      session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, oversized_ciphertext,
                          attr, std::size(attr), &handle));
}

TEST_F(TestSessionWithTpmSimulator, WrapKeyRSAOAEPWithHWSec) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));
  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  int handle = 0;
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const Object* aeskey_new = nullptr;
  session_->GetObject(handle, &aeskey_new);
  EXPECT_EQ(aes_size, aeskey_new->GetAttributeInt(CKA_VALUE_LEN, 0));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_LOCAL, true));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_ALWAYS_SENSITIVE, true));
  EXPECT_EQ(false, aeskey_new->GetAttributeBool(CKA_NEVER_EXTRACTABLE, true));
  EXPECT_EQ(true, aeskey_new->GetAttributeBool(CKA_EXTRACTABLE, false));
  EXPECT_EQ(aeskey->GetAttributeString(CKA_VALUE),
            aeskey_new->GetAttributeString(CKA_VALUE));
}
#endif  // USE_TPM2

TEST_F(TestSessionWithRealObject, WrapKeyRSAOAEPInvalidAttributes) {
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  int rsa_size = 2048;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_WRAP, &no, sizeof(no)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &rsa_size, sizeof(rsa_size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_UNWRAP, &no, sizeof(no)},
                              {CKA_EXTRACTABLE, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object *rsapub, *rsapriv;
  ASSERT_TRUE(session_->GetObject(pubh, &rsapub));
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv));

  const Object* aeskey = nullptr;
  int aes_size = 32;
  GenerateSecretKey(CKM_AES_KEY_GEN, aes_size, &aeskey);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_WRAP, true);
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_UNWRAP, true);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_UNEXTRACTABLE)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_FUNCTION_NOT_PERMITTED)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_MECHANISM_PARAM_INVALID)))
      .Times(1);
  EXPECT_CALL(
      mock_metrics_library_,
      SendSparseToUMA(kChapsSessionWrapKey,
                      static_cast<int>(CKR_WRAPPING_KEY_TYPE_INCONSISTENT)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_KEY_NOT_WRAPPABLE)))
      .Times(2);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKey, static_cast<int>(CKR_OK)))
      .Times(2);

  int len = 0;
  string wrapped_key;
  // The key being wrapped doesn't have CKA_EXTRACTABLE attribute set to
  // CK_TRUE.
  EXPECT_EQ(CKR_KEY_UNEXTRACTABLE,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_EXTRACTABLE, true);

  // The wrapping_key doesn't have CKA_WRAP attribute set to CK_TRUE.
  EXPECT_EQ(CKR_KEY_FUNCTION_NOT_PERMITTED,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  const_cast<Object*>(rsapub)->SetAttributeBool(CKA_WRAP, true);

  // Non-empty mechanism_parameter is not supported for CKM_RSA_PKCS_OAEP.
  EXPECT_EQ(CKR_MECHANISM_PARAM_INVALID,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "custom_oaep_params", rsapub,
                              *aeskey, &len, &wrapped_key));

  // The wrapping_key should be a rsa public key.
  EXPECT_EQ(CKR_WRAPPING_KEY_TYPE_INCONSISTENT,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", aeskey, *aeskey, &len,
                              &wrapped_key));

  // Cannot wrap private keys using CKM_RSA_PKCS_OAEP.
  EXPECT_EQ(CKR_KEY_NOT_WRAPPABLE,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *rsapriv, &len,
                              &wrapped_key));

  // WrapKey should now success.
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));

  // Setting CKA_WRAP_WITH_TRUSTED=true for the target should disable WrapKey.
  const_cast<Object*>(aeskey)->SetAttributeBool(CKA_WRAP_WITH_TRUSTED, true);
  EXPECT_EQ(CKR_KEY_NOT_WRAPPABLE,
            session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey, &len,
                              &wrapped_key));

  // Setting CKA_TRUSTED=true for the wrapping should enable WrapKey again.
  // (p.s. normally chaps cannot set CKA_TRUSTED=true for any key.)
  const_cast<Object*>(rsapub)->SetAttributeBool(CKA_TRUSTED, true);
  EXPECT_EQ(CKR_OK, session_->WrapKey(CKM_RSA_PKCS_OAEP, "", rsapub, *aeskey,
                                      &len, &wrapped_key));

  CK_KEY_TYPE key_type = CKK_AES;
  CK_ATTRIBUTE attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                         {CKA_VALUE_LEN, &aes_size, sizeof(aes_size)},
                         {CKA_KEY_TYPE, &key_type, sizeof(key_type)}};

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_KEY_FUNCTION_NOT_PERMITTED)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_MECHANISM_PARAM_INVALID)))
      .Times(1);
  EXPECT_CALL(
      mock_metrics_library_,
      SendSparseToUMA(kChapsSessionUnwrapKey,
                      static_cast<int>(CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT)))
      .Times(2);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey,
                              static_cast<int>(CKR_FUNCTION_FAILED)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKey, static_cast<int>(CKR_OK)))
      .Times(1);

  int handle = 0;
  // The unwrapping_key doesn't have CKA_WRAP attribute set to CK_TRUE.
  EXPECT_EQ(CKR_KEY_FUNCTION_NOT_PERMITTED,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const_cast<Object*>(rsapriv)->SetAttributeBool(CKA_UNWRAP, true);

  // Non-empty mechanism_parameter is not supported for CKM_RSA_PKCS_OAEP.
  EXPECT_EQ(
      CKR_MECHANISM_PARAM_INVALID,
      session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "custom_oaep_params", rsapriv,
                          wrapped_key, attr, std::size(attr), &handle));

  // The unwrapping_key should be a rsa private key.
  EXPECT_EQ(CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", aeskey, wrapped_key,
                                attr, std::size(attr), &handle));

  // Missing CKA_MODULUS on the unwrapping key returns
  // CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT.
  string saved_modulus = rsapriv->GetAttributeString(CKA_MODULUS);
  const_cast<Object*>(rsapriv)->RemoveAttribute(CKA_MODULUS);
  EXPECT_EQ(CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
  const_cast<Object*>(rsapriv)->SetAttributeString(CKA_MODULUS, saved_modulus);

  // Generate another rsa key pair and unwrap the blob using the new
  // (mismatched) private key.
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* rsapriv2;
  ASSERT_TRUE(session_->GetObject(privh, &rsapriv2));
  const_cast<Object*>(rsapriv2)->SetAttributeBool(CKA_UNWRAP, true);
  EXPECT_EQ(CKR_FUNCTION_FAILED,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv2, wrapped_key,
                                attr, std::size(attr), &handle));

  // Unwrap should now succeed.
  EXPECT_EQ(CKR_OK,
            session_->UnwrapKey(CKM_RSA_PKCS_OAEP, "", rsapriv, wrapped_key,
                                attr, std::size(attr), &handle));
}

#if USE_TPM2
TEST_F(TestSessionWithTpmSimulator, WrapKeyWithChaps) {
  const Object *rsapub, *rsapriv;
  GenerateRSAKeyPair(true, 1024, &rsapub, &rsapriv);

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKeyWithChaps,
                              static_cast<int>(CKR_KEY_NOT_WRAPPABLE)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionWrapKeyWithChaps,
                              static_cast<int>(CKR_BUFFER_TOO_SMALL)))
      .Times(1);
  EXPECT_CALL(
      mock_metrics_library_,
      SendSparseToUMA(kChapsSessionWrapKeyWithChaps, static_cast<int>(CKR_OK)))
      .Times(1);
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionUnwrapKeyWithChaps,
                              static_cast<int>(CKR_OK)))
      .Times(1);

  int len = 0;
  string wrapped_key;
  EXPECT_EQ(CKR_KEY_NOT_WRAPPABLE,
            session_->WrapKey(kChapsKeyWrapMechanism, "", nullptr, *rsapriv,
                              &len, &wrapped_key));
  const_cast<Object*>(rsapriv)->SetAttributeBool(kChapsWrappableAttribute,
                                                 true);
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->WrapKey(kChapsKeyWrapMechanism, "", nullptr, *rsapriv,
                              &len, &wrapped_key));
  EXPECT_EQ(CKR_OK, session_->WrapKey(kChapsKeyWrapMechanism, "", nullptr,
                                      *rsapriv, &len, &wrapped_key));

  int handle = 0;
  EXPECT_EQ(CKR_OK, session_->UnwrapKey(kChapsKeyWrapMechanism, "", nullptr,
                                        wrapped_key, {}, 0, &handle));
  const Object* rsapriv_new = nullptr;
  session_->GetObject(handle, &rsapriv_new);
  EXPECT_TRUE(rsapriv_new->GetAttributeBool(CKA_LOCAL, false));
  EXPECT_TRUE(rsapriv_new->GetAttributeBool(kChapsWrappableAttribute, false));
  EXPECT_TRUE(rsapriv_new->GetAttributeBool(CKA_ALWAYS_SENSITIVE, false));
  EXPECT_TRUE(rsapriv_new->GetAttributeBool(CKA_NEVER_EXTRACTABLE, false));
  EXPECT_EQ(rsapriv->GetAttributeString(kKeyBlobAttribute),
            rsapriv_new->GetAttributeString(kKeyBlobAttribute));
}
#endif  // USE_TPM2

TEST_F(TestSession, CreateObjectsNoPrivate) {
  EXPECT_CALL(token_pool_, Insert(_))
      .WillRepeatedly(Return(ObjectPool::Result::WaitForPrivateObjects));

  int handle = 0;
  int size = 2048;
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;

  CK_OBJECT_CLASS oc = CKO_SECRET_KEY;
  CK_ATTRIBUTE attr[] = {{CKA_CLASS, &oc, sizeof(oc)}};
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->CreateObject(attr, std::size(attr), &handle));

  CK_ATTRIBUTE key_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_SIGN, &yes, sizeof(yes)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_VALUE_LEN, &size, sizeof(size)}};
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->GenerateKey(CKM_GENERIC_SECRET_KEY_GEN, "", key_attr, 4,
                                  &handle));

  CK_BYTE pubexp[] = {1, 0, 1};
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_ENCRYPT, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &no, sizeof(CK_BBOOL)},
                              {CKA_DECRYPT, &no, sizeof(no)},
                              {CKA_SIGN, &yes, sizeof(yes)}};
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      5, priv_attr, 3, &handle, &handle));
}

TEST_F(TestSession, FindObjectsNoPrivate) {
  EXPECT_CALL(token_pool_, Find(_, _))
      .WillRepeatedly(Return(ObjectPool::Result::WaitForPrivateObjects));

  CK_OBJECT_CLASS oc = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE attr[] = {{CKA_CLASS, &oc, sizeof(oc)}};
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->FindObjectsInit(attr, std::size(attr)));
}

TEST_F(TestSession, DestroyObjectsNoPrivate) {
  EXPECT_CALL(token_pool_, Delete(_))
      .WillRepeatedly(Return(ObjectPool::Result::WaitForPrivateObjects));

  int handle = 0;

  CK_OBJECT_CLASS oc = CKO_SECRET_KEY;
  CK_ATTRIBUTE attr[] = {{CKA_CLASS, &oc, sizeof(oc)}};
  ASSERT_EQ(CKR_OK, session_->CreateObject(attr, std::size(attr), &handle));
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->DestroyObject(handle));
}

TEST_F(TestSession, FlushObjectsNoPrivate) {
  EXPECT_CALL(token_pool_, Flush(_))
      .WillRepeatedly(Return(ObjectPool::Result::WaitForPrivateObjects));

  ObjectMock token_object;
  EXPECT_CALL(token_object, IsTokenObject()).WillRepeatedly(Return(true));
  EXPECT_EQ(CKR_WOULD_BLOCK_FOR_PRIVATE_OBJECTS,
            session_->FlushModifiableObject(&token_object));
}

TEST_F(TestSession, MultipleSignWithHWSec) {
  EXPECT_CALL(hwsec_, IsECCurveSupported(_))
      .WillRepeatedly(ReturnOk<TPMError>());
  EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
  EXPECT_CALL(hwsec_, GetECCPublicKey(_))
      .WillRepeatedly(ReturnValue(GenerateECCPublicInfo()));

  EXPECT_CALL(hwsec_, GenerateECCKey(
                          _, _, hwsec::ChapsFrontend::AllowDecrypt::kNotAllow,
                          hwsec::ChapsFrontend::AllowSign::kAllow))
      .WillOnce([&](auto&&, auto&&, auto&&, auto&&) {
        return hwsec::ChapsFrontend::CreateKeyResult{
            .key = GetTestScopedKey(),
        };
      });

  const Object* pub = nullptr;
  const Object* priv = nullptr;
  GenerateECCKeyPair(true, true, &pub, &priv);

  EXPECT_CALL(hwsec_, LoadKey(_, _)).WillRepeatedly([&](auto&&, auto&&) {
    return GetTestScopedKey();
  });
  EXPECT_CALL(hwsec_, Sign(_, _, _))
      .WillRepeatedly(ReturnValue(brillo::Blob()));

  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kChapsSessionSign, static_cast<int>(CKR_OK)))
      .WillRepeatedly(Return(true));

  for (int i = 0; i < 100; i++) {
    EXPECT_EQ(CKR_OK, session_->OperationInit(kSign, CKM_ECDSA_SHA1, "", priv));
    string in(100, 'A');
    int len = 0;
    string sig;
    EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kSign, in, &len, &sig));
  }

  EXPECT_EQ(session_->get_object_key_map_size_for_testing(), 0);
}

TEST_F(TestSession, DestroyObjectDuringActiveOperation) {
  const Object* priv = nullptr;
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  CK_ULONG size = 1024;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &no, sizeof(CK_BBOOL)},
                              {CKA_SIGN, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  ASSERT_TRUE(session_->GetObject(privh, &priv));

  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionSign, _))
      .Times(AnyNumber());

  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_SHA256_RSA_PKCS, "", priv));
  EXPECT_TRUE(session_->IsOperationActive(kSign));

  // Destroying the object removes the handle from the pool, but the active
  // operation retains shared ownership of the key object and continues safely.
  EXPECT_EQ(CKR_OK, session_->DestroyObject(privh));
  EXPECT_FALSE(session_->GetObject(privh, &priv));
  EXPECT_TRUE(session_->IsOperationActive(kSign));

  int len = 0;
  string sig;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL, session_->OperationFinal(kSign, &len, &sig));
  EXPECT_GT(len, 0);
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kSign, &len, &sig));
  EXPECT_FALSE(session_->IsOperationActive(kSign));
}

TEST_F(TestSession, DestroyTokenObjectCrossSessionDuringActiveOperation) {
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  CK_ULONG size = 1024;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                             {CKA_VERIFY, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &yes, sizeof(yes)},
                              {CKA_SIGN, &yes, sizeof(yes)},
                              {kForceSoftwareAttribute, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));

  const Object* priv = nullptr;
  ASSERT_TRUE(session_->GetObject(privh, &priv));

  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionSign, _))
      .Times(AnyNumber());

  // Session 1 initializes Sign operation with the token private key.
  EXPECT_EQ(CKR_OK,
            session_->OperationInit(kSign, CKM_SHA256_RSA_PKCS, "", priv));
  EXPECT_TRUE(session_->IsOperationActive(kSign));

  // Create a second session sharing the same token object pool and slot.
  auto session2 =
      std::make_unique<SessionImpl>(1, &token_pool_, &hwsec_, &factory_,
                                    &handle_generator_, false, &chaps_metrics_);

  // Session 2 destroys the token object while Session 1 has an active
  // operation.
  EXPECT_EQ(CKR_OK, session2->DestroyObject(privh));

  // The object handle is removed from the token pool (Session 2 cannot get it).
  const Object* check_obj = nullptr;
  EXPECT_FALSE(session2->GetObject(privh, &check_obj));

  // Session 1's operation context is preserved and the underlying object memory
  // is kept alive via shared ownership, avoiding use-after-free.
  EXPECT_TRUE(session_->IsOperationActive(kSign));

  // Session 1 continues and finalizes the operation safely without memory
  // corruption.
  int len = 0;
  string sig;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL, session_->OperationFinal(kSign, &len, &sig));
  EXPECT_GT(len, 0);
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kSign, &len, &sig));
  EXPECT_FALSE(session_->IsOperationActive(kSign));
}

TEST_F(TestSession, DestroyObjectDuringMultipartEncryptDecrypt) {
  const Object* pub = nullptr;
  const Object* priv = nullptr;
  CK_BBOOL no = CK_FALSE;
  CK_BBOOL yes = CK_TRUE;
  CK_BYTE pubexp[] = {1, 0, 1};
  CK_ULONG size = 1024;
  CK_ATTRIBUTE pub_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                             {CKA_ENCRYPT, &yes, sizeof(yes)},
                             {CKA_PUBLIC_EXPONENT, pubexp, 3},
                             {CKA_MODULUS_BITS, &size, sizeof(size)}};
  CK_ATTRIBUTE priv_attr[] = {{CKA_TOKEN, &no, sizeof(no)},
                              {CKA_DECRYPT, &yes, sizeof(yes)}};
  int pubh = 0, privh = 0;
  ASSERT_EQ(CKR_OK,
            session_->GenerateKeyPair(CKM_RSA_PKCS_KEY_PAIR_GEN, "", pub_attr,
                                      std::size(pub_attr), priv_attr,
                                      std::size(priv_attr), &pubh, &privh));
  ASSERT_TRUE(session_->GetObject(privh, &priv));
  ASSERT_TRUE(session_->GetObject(pubh, &pub));

  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionEncrypt, _))
      .Times(AnyNumber());
  EXPECT_CALL(mock_metrics_library_, SendSparseToUMA(kChapsSessionDecrypt, _))
      .Times(AnyNumber());

  // Encrypt plaintext.
  EXPECT_EQ(CKR_OK, session_->OperationInit(kEncrypt, CKM_RSA_PKCS, "", pub));
  string plaintext(64, 'X');
  int cipher_len = 0;
  string ciphertext;
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationSinglePart(kEncrypt, plaintext, &cipher_len,
                                          &ciphertext));
  EXPECT_EQ(CKR_OK, session_->OperationSinglePart(kEncrypt, plaintext,
                                                  &cipher_len, &ciphertext));

  // Initialize multipart Decrypt with the private key.
  EXPECT_EQ(CKR_OK, session_->OperationInit(kDecrypt, CKM_RSA_PKCS, "", priv));
  EXPECT_TRUE(session_->IsOperationActive(kDecrypt));

  // Destroying the key object during an active multipart decrypt operation.
  EXPECT_EQ(CKR_OK, session_->DestroyObject(privh));
  EXPECT_FALSE(session_->GetObject(privh, &priv));
  EXPECT_TRUE(session_->IsOperationActive(kDecrypt));

  int out_len = 0;
  string decrypted;
  EXPECT_EQ(CKR_OK, session_->OperationUpdate(kDecrypt, ciphertext, &out_len,
                                              &decrypted));
  EXPECT_EQ(CKR_BUFFER_TOO_SMALL,
            session_->OperationFinal(kDecrypt, &out_len, &decrypted));
  EXPECT_GT(out_len, 0);
  EXPECT_EQ(CKR_OK, session_->OperationFinal(kDecrypt, &out_len, &decrypted));
  EXPECT_FALSE(session_->IsOperationActive(kDecrypt));
  EXPECT_EQ(decrypted, plaintext);
}

}  // namespace chaps

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  OpenSSL_add_all_algorithms();
  ERR_load_crypto_strings();
  return RUN_ALL_TESTS();
}
