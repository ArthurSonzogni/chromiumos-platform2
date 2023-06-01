// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_auth_stack_manager.h"

#include <utility>

#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "biod/cros_fp_device.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_cros_fp_device.h"
#include "biod/mock_cros_fp_session_manager.h"
#include "biod/mock_pairing_key_storage.h"
#include "biod/power_button_filter.h"

namespace biod {

using EnrollStatus = AuthStackManager::EnrollStatus;
using Mode = ec::FpMode::Mode;
using State = CrosFpAuthStackManager::State;
using GetSecretReply = ec::CrosFpDeviceInterface::GetSecretReply;
using KeygenReply = ec::CrosFpDeviceInterface::PairingKeyKeygenReply;

using brillo::BlobToString;

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::InSequence;
using testing::Pointee;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::SetArgPointee;

namespace {

CreateCredentialRequest MakeCreateCredentialRequest(
    const std::string& user_id,
    const brillo::Blob& gsc_nonce,
    const brillo::Blob& encrypted_label_seed,
    const brillo::Blob& iv,
    const brillo::Blob& pub_x,
    const brillo::Blob& pub_y) {
  CreateCredentialRequest request;
  request.set_user_id(user_id);
  request.set_gsc_nonce(BlobToString(gsc_nonce));
  request.set_encrypted_label_seed(BlobToString(encrypted_label_seed));
  request.set_iv(BlobToString(iv));
  request.mutable_pub()->set_x(BlobToString(pub_x));
  request.mutable_pub()->set_y(BlobToString(pub_y));
  return request;
}

}  // namespace

MATCHER_P(EnrollProgressIs, progress, "") {
  return arg.percent_complete == progress && arg.done == (progress == 100);
}

class CrosFpAuthStackManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const auto mock_bus = base::MakeRefCounted<dbus::MockBus>(options);

    const auto power_manager_proxy =
        base::MakeRefCounted<dbus::MockObjectProxy>(
            mock_bus.get(), power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    EXPECT_CALL(*mock_bus,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(testing::Return(power_manager_proxy.get()));

    auto mock_cros_dev = std::make_unique<MockCrosFpDevice>();
    // Keep a pointer to the fake device to manipulate it later.
    mock_cros_dev_ = mock_cros_dev.get();

    auto mock_session_manager = std::make_unique<MockCrosFpSessionManager>();
    // Keep a pointer to record manager, to manipulate it later.
    mock_session_manager_ = mock_session_manager.get();

    auto mock_pk_storage = std::make_unique<MockPairingKeyStorage>();
    mock_pk_storage_ = mock_pk_storage.get();

    // Always support positive match secret
    EXPECT_CALL(*mock_cros_dev_, SupportsPositiveMatchSecret())
        .WillRepeatedly(Return(true));

    // Save OnMkbpEvent callback to use later in tests
    ON_CALL(*mock_cros_dev_, SetMkbpEventCallback)
        .WillByDefault(SaveArg<0>(&on_mkbp_event_));

    cros_fp_auth_stack_manager_ = std::make_unique<CrosFpAuthStackManager>(
        PowerButtonFilter::Create(mock_bus), std::move(mock_cros_dev),
        &mock_metrics_, std::move(mock_session_manager),
        std::move(mock_pk_storage));

    cros_fp_auth_stack_manager_->SetEnrollScanDoneHandler(
        base::BindRepeating(&CrosFpAuthStackManagerTest::EnrollScanDoneHandler,
                            base::Unretained(this)));
  }

 protected:
  MOCK_METHOD(void,
              EnrollScanDoneHandler,
              (ScanResult, const EnrollStatus&, brillo::Blob));

  metrics::MockBiodMetrics mock_metrics_;
  std::unique_ptr<CrosFpAuthStackManager> cros_fp_auth_stack_manager_;
  MockPairingKeyStorage* mock_pk_storage_;
  MockCrosFpSessionManager* mock_session_manager_;
  MockCrosFpDevice* mock_cros_dev_;
  CrosFpDevice::MkbpCallback on_mkbp_event_;
};

TEST_F(CrosFpAuthStackManagerTest, TestGetType) {
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetType(), BIOMETRIC_TYPE_FINGERPRINT);
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionSuccess) {
  const std::optional<std::string> kUserId("testuser");
  AuthStackManager::Session enroll_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  // Expect biod will check if there is space for a new template.
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));

  // Expect that biod will ask FPMCU to set the enroll mode.
  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(true));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_TRUE(enroll_session);

  // When enroll session ends, FP mode will be set to kNone.
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionTwiceFailed) {
  const std::optional<std::string> kUserId("testuser");
  AuthStackManager::Session first_enroll_session;
  AuthStackManager::Session second_enroll_session;

  EXPECT_CALL(*mock_session_manager_, GetUser)
      .WillRepeatedly(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillRepeatedly(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillRepeatedly(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillRepeatedly(Return(true));

  first_enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  ASSERT_TRUE(first_enroll_session);

  second_enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_FALSE(second_enroll_session);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollSessionEnrollModeFailed) {
  const std::optional<std::string> kUserId("testuser");
  AuthStackManager::Session enroll_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(false));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_FALSE(enroll_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollSessionNoUser) {
  const std::optional<std::string> kNoUserId = std::nullopt;
  AuthStackManager::Session enroll_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  EXPECT_FALSE(enroll_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestDoEnrollImageEventSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kNonce(32, 1);
  // Start an enrollment sessions without performing all checks since this is
  // already tested by TestStartEnrollSessionSuccess.
  AuthStackManager::Session enroll_session;
  EXPECT_CALL(*mock_session_manager_, GetUser)
      .WillRepeatedly(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_, ResetContext).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(_)).WillRepeatedly(Return(true));
  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession();
  ASSERT_TRUE(enroll_session);

  EXPECT_CALL(*mock_cros_dev_, GetNonce).WillOnce(Return(kNonce));

  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_IMMOBILE,
                                    EnrollProgressIs(25), brillo::Blob()));
  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_PARTIAL,
                                    EnrollProgressIs(50), brillo::Blob()));
  EXPECT_CALL(*this,
              EnrollScanDoneHandler(ScanResult::SCAN_RESULT_INSUFFICIENT,
                                    EnrollProgressIs(75), brillo::Blob()));
  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_SUCCESS,
                                           EnrollProgressIs(100), kNonce));

  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_IMMOBILE |
                     25 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE |
                     50 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY |
                     75 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERR_ENROLL_OK |
                     100 << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeLoadPairingKey) {
  const brillo::Blob kWrappedPk(32, 1);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_pk_storage_, ReadWrappedPairingKey)
      .WillOnce(Return(kWrappedPk));
  EXPECT_CALL(*mock_cros_dev_, LoadPairingKey(kWrappedPk))
      .WillOnce(Return(true));

  EXPECT_TRUE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeLoadPairingKeyReadFailed) {
  const brillo::Blob kWrappedPk(32, 1);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_pk_storage_, ReadWrappedPairingKey)
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeLoadPairingKeyLoadFailed) {
  const brillo::Blob kWrappedPk(32, 1);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_pk_storage_, ReadWrappedPairingKey)
      .WillOnce(Return(kWrappedPk));
  EXPECT_CALL(*mock_cros_dev_, LoadPairingKey(kWrappedPk))
      .WillOnce(Return(false));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeNoPk) {
  // TODO(b/251738584): Test more behavior here after biod starts to establish
  // Pk automatically on boot.
  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_TRUE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1))
      .WillOnce(Return(ByMove(std::make_unique<VendorTemplate>(kTemplate))));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(-1, kPubInX, kPubInY))
      .WillOnce(Return(GetSecretReply{
          .encrypted_secret = kEncryptedSecret,
          .iv = kSecretIv,
          .pk_out_x = kPubOutX,
          .pk_out_y = kPubOutY,
      }));
  EXPECT_CALL(*mock_session_manager_, CreateRecord(_, Pointee(kTemplate)))
      .WillOnce(Return(true));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  ASSERT_EQ(reply.status(), CreateCredentialReply::SUCCESS);
  EXPECT_EQ(reply.encrypted_secret(), BlobToString(kEncryptedSecret));
  EXPECT_EQ(reply.iv(), BlobToString(kSecretIv));
  EXPECT_EQ(reply.pub().x(), BlobToString(kPubOutX));
  EXPECT_EQ(reply.pub().y(), BlobToString(kPubOutY));
  EXPECT_FALSE(reply.record_id().empty());
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialNotReady) {
  const std::string kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialNoUser) {
  const std::string kUserId("testuser");
  const std::optional<std::string> kNoUserId = std::nullopt;
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialIncorrectUser) {
  const std::string kUserId("testuser"), kFakeUserId("fakeuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kFakeUserId));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialSetNonceFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(false));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::SET_NONCE_FAILED);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialGetTemplateFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1)).Times(1);

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::NO_TEMPLATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialGetSecretFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kTemplate(10, 8);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1))
      .WillOnce(Return(ByMove(std::make_unique<VendorTemplate>(kTemplate))));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(-1, kPubInX, kPubInY))
      .WillOnce(Return(std::nullopt));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::NO_SECRET);
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialPersistRecordFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

  cros_fp_auth_stack_manager_->SetStateForTest(State::kEnrollDone);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1))
      .WillOnce(Return(ByMove(std::make_unique<VendorTemplate>(kTemplate))));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(-1, kPubInX, kPubInY))
      .WillOnce(Return(GetSecretReply{
          .encrypted_secret = kEncryptedSecret,
          .iv = kSecretIv,
          .pk_out_x = kPubOutX,
          .pk_out_y = kPubOutY,
      }));
  EXPECT_CALL(*mock_session_manager_, CreateRecord(_, Pointee(kTemplate)))
      .WillOnce(Return(false));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::CREATE_RECORD_FAILED);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedIn) {
  const std::string kUserId("testuser");
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId));
  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedOut) {
  const std::string kUserId("testuser");
  EXPECT_CALL(*mock_session_manager_, UnloadUser);
  cros_fp_auth_stack_manager_->OnUserLoggedOut();
}

}  // namespace biod
