// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_auth_stack_manager.h"

#include <utility>

#include <base/time/time.h>
#include <base/test/task_environment.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/factory/mock_factory.h>
#include <libhwsec/frontend/pinweaver_manager/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "biod/cros_fp_device.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_cros_fp_device.h"
#include "biod/mock_cros_fp_session_manager.h"
#include "biod/mock_pairing_key_storage.h"
#include "biod/mock_power_button_filter.h"

namespace biod {

using EnrollStatus = AuthStackManager::EnrollStatus;
using Mode = ec::FpMode::Mode;
using State = CrosFpAuthStackManager::State;
using GetSecretReply = ec::CrosFpDeviceInterface::GetSecretReply;
using KeygenReply = ec::CrosFpDeviceInterface::PairingKeyKeygenReply;

using brillo::BlobToString;

using PinWeaverEccPoint = hwsec::PinWeaverManagerFrontend::PinWeaverEccPoint;

using hwsec::TPMError;
using hwsec::TPMRetryAction;
using hwsec::PinWeaverManagerFrontend::AuthChannel::kFingerprintAuthChannel;

using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;

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

StartEnrollSessionRequest MakeStartEnrollSessionRequest(
    const brillo::Blob& gsc_nonce,
    const brillo::Blob& encrypted_label_seed,
    const brillo::Blob& iv) {
  StartEnrollSessionRequest request;
  request.set_gsc_nonce(BlobToString(gsc_nonce));
  request.set_encrypted_label_seed(BlobToString(encrypted_label_seed));
  request.set_iv(BlobToString(iv));
  return request;
}

CreateCredentialRequest MakeCreateCredentialRequest(const brillo::Blob& pub_x,
                                                    const brillo::Blob& pub_y) {
  CreateCredentialRequest request;
  request.mutable_pub()->set_x(BlobToString(pub_x));
  request.mutable_pub()->set_y(BlobToString(pub_y));
  return request;
}

StartAuthSessionRequest MakeStartAuthSessionRequest(
    const std::string& user_id,
    const brillo::Blob& gsc_nonce,
    const brillo::Blob& encrypted_label_seed,
    const brillo::Blob& iv) {
  StartAuthSessionRequest request;
  request.set_user_id(user_id);
  request.set_gsc_nonce(BlobToString(gsc_nonce));
  request.set_encrypted_label_seed(BlobToString(encrypted_label_seed));
  request.set_iv(BlobToString(iv));
  return request;
}

AuthenticateCredentialRequest MakeAuthenticateCredentialRequest(
    const brillo::Blob& pub_x, const brillo::Blob& pub_y) {
  AuthenticateCredentialRequest request;
  request.mutable_pub()->set_x(BlobToString(pub_x));
  request.mutable_pub()->set_y(BlobToString(pub_y));
  return request;
}

EnrollLegacyTemplateRequest MakeEnrollLegacyTemplateRequest(
    const std::string& user_id,
    const brillo::Blob& gsc_nonce,
    const brillo::Blob& encrypted_label_seed,
    const brillo::Blob& iv) {
  EnrollLegacyTemplateRequest request;
  request.set_legacy_record_id(user_id);
  request.set_gsc_nonce(BlobToString(gsc_nonce));
  request.set_encrypted_label_seed(BlobToString(encrypted_label_seed));
  request.set_iv(BlobToString(iv));
  return request;
}

}  // namespace

MATCHER_P(EnrollProgressIs, progress, "") {
  return arg.percent_complete == progress && arg.done == (progress == 100);
}

// Using a peer class to control access to the class under test is better than
// making the text fixture a friend class.
class CrosFpAuthStackManagerPeer {
 public:
  explicit CrosFpAuthStackManagerPeer(
      std::unique_ptr<CrosFpAuthStackManager> cros_fp_biometrics_manager)
      : cros_fp_auth_stack_manager_(std::move(cros_fp_biometrics_manager)) {}

  // Methods to execute CrosFpAuthStackManager private methods.

  void RequestFingerUp() { cros_fp_auth_stack_manager_->RequestFingerUp(); }

 private:
  std::unique_ptr<CrosFpAuthStackManager> cros_fp_auth_stack_manager_;
};

class CrosFpAuthStackManagerTest : public ::testing::Test {
 public:
  void SetUp() override { SetUpWithInitialState(); }

  void SetUpWithInitialState(
      State state = State::kNone,
      std::optional<uint32_t> pending_match_event = std::nullopt) {
    auto mock_power_button_filter = std::make_unique<MockPowerButtonFilter>();
    mock_power_button_filter_ = mock_power_button_filter.get();
    ON_CALL(*mock_power_button_filter_, ShouldFilterFingerprintMatch)
        .WillByDefault(Return(false));

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

    auto mock_pinweaver_manager =
        std::make_unique<hwsec::MockPinWeaverManagerFrontend>();
    mock_pinweaver_manager_ = mock_pinweaver_manager.get();

    auto mock_legacy_session_manager =
        std::make_unique<MockCrosFpSessionManager>();
    // Keep a pointer to session manager, to manipulate it later.
    mock_legacy_session_manager_ = mock_legacy_session_manager.get();

    auto cros_fp_auth_stack_manager = std::make_unique<CrosFpAuthStackManager>(
        std::move(mock_power_button_filter), std::move(mock_cros_dev),
        &mock_metrics_, std::move(mock_session_manager),
        std::move(mock_pk_storage), std::move(mock_pinweaver_manager),
        std::move(mock_legacy_session_manager), state, pending_match_event);

    cros_fp_auth_stack_manager->SetEnrollScanDoneHandler(
        base::BindRepeating(&CrosFpAuthStackManagerTest::EnrollScanDoneHandler,
                            base::Unretained(this)));
    cros_fp_auth_stack_manager->SetAuthScanDoneHandler(
        base::BindRepeating(&CrosFpAuthStackManagerTest::AuthScanDoneHandler,
                            base::Unretained(this)));
    cros_fp_auth_stack_manager_ = cros_fp_auth_stack_manager.get();

    cros_fp_auth_stack_manager_peer_.emplace(
        std::move(cros_fp_auth_stack_manager));
  }

  MOCK_METHOD(void, AuthScanDoneHandler, ());

 protected:
  MOCK_METHOD(void, EnrollScanDoneHandler, (ScanResult, const EnrollStatus&));

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::optional<CrosFpAuthStackManagerPeer> cros_fp_auth_stack_manager_peer_;
  metrics::MockBiodMetrics mock_metrics_;
  CrosFpAuthStackManager* cros_fp_auth_stack_manager_;
  MockPowerButtonFilter* mock_power_button_filter_;
  MockPairingKeyStorage* mock_pk_storage_;
  MockCrosFpSessionManager* mock_session_manager_;
  MockCrosFpDevice* mock_cros_dev_;
  hwsec::MockPinWeaverManagerFrontend* mock_pinweaver_manager_;
  MockCrosFpSessionManager* mock_legacy_session_manager_;
  CrosFpDevice::MkbpCallback on_mkbp_event_;
};

TEST_F(CrosFpAuthStackManagerTest, TestGetType) {
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetType(), BIOMETRIC_TYPE_FINGERPRINT);
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session enroll_session;

  auto request = MakeStartEnrollSessionRequest(kGscNonce, kEncryptedLabelSeed,
                                               kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  // Expect biod will check if there is space for a new template.
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(3));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));

  // Expect that biod will ask FPMCU to set the enroll mode.
  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(true));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession(request);
  EXPECT_TRUE(enroll_session);

  // When enroll session ends, FP mode will be set to kNone.
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestStartEnrollSessionTwiceFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session first_enroll_session;
  AuthStackManager::Session second_enroll_session;

  auto request = MakeStartEnrollSessionRequest(kGscNonce, kEncryptedLabelSeed,
                                               kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser)
      .WillRepeatedly(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillRepeatedly(Return(2));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(0)).WillRepeatedly(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillRepeatedly(Return(true));

  first_enroll_session =
      cros_fp_auth_stack_manager_->StartEnrollSession(request);
  ASSERT_TRUE(first_enroll_session);

  second_enroll_session =
      cros_fp_auth_stack_manager_->StartEnrollSession(request);
  EXPECT_FALSE(second_enroll_session);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollSessionEnrollModeFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session enroll_session;

  auto request = MakeStartEnrollSessionRequest(kGscNonce, kEncryptedLabelSeed,
                                               kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(0)).WillRepeatedly(Return(true));

  EXPECT_CALL(*mock_cros_dev_,
              SetFpMode(ec::FpMode(Mode::kEnrollSessionEnrollImage)))
      .WillOnce(Return(false));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession(request);
  EXPECT_FALSE(enroll_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollSessionNoUser) {
  const std::optional<std::string> kNoUserId = std::nullopt;
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session enroll_session;

  auto request = MakeStartEnrollSessionRequest(kGscNonce, kEncryptedLabelSeed,
                                               kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));

  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession(request);
  EXPECT_FALSE(enroll_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestDoEnrollImageEventSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  // Start an enrollment sessions without performing all checks since this is
  // already tested by TestStartEnrollSessionSuccess.
  AuthStackManager::Session enroll_session;

  auto request = MakeStartEnrollSessionRequest(kGscNonce, kEncryptedLabelSeed,
                                               kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser)
      .WillRepeatedly(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(1));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(0)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(_)).WillRepeatedly(Return(true));
  enroll_session = cros_fp_auth_stack_manager_->StartEnrollSession(request);
  ASSERT_TRUE(enroll_session);

  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_IMMOBILE,
                                           EnrollProgressIs(25)));
  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_PARTIAL,
                                           EnrollProgressIs(50)));
  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_INSUFFICIENT,
                                           EnrollProgressIs(75)));
  EXPECT_CALL(*this, EnrollScanDoneHandler(ScanResult::SCAN_RESULT_SUCCESS,
                                           EnrollProgressIs(100)));

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
  const brillo::Blob kPubX(32, 1);
  const brillo::Blob kPubY(32, 2);
  const brillo::Blob kEncryptedPriv(32, 3);
  const brillo::Blob kEncryptedPk(32, 4);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_CALL(*mock_pinweaver_manager_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_manager_, GetVersion).WillOnce(ReturnValue(2));
  EXPECT_CALL(*mock_cros_dev_, PairingKeyKeygen)
      .WillOnce(ReturnValue(KeygenReply{
          .pub_x = kPubX,
          .pub_y = kPubY,
          .encrypted_private_key = kEncryptedPriv,
      }));
  EXPECT_CALL(*mock_pinweaver_manager_, GeneratePk(kFingerprintAuthChannel, _))
      .WillOnce(ReturnValue(PinWeaverEccPoint()));
  EXPECT_CALL(*mock_cros_dev_, PairingKeyWrap(_, _, kEncryptedPriv))
      .WillOnce(ReturnValue(kEncryptedPk));
  EXPECT_CALL(*mock_pk_storage_, WriteWrappedPairingKey(kEncryptedPk))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_pk_storage_, ReadWrappedPairingKey)
      .WillOnce(Return(kEncryptedPk));
  EXPECT_CALL(*mock_cros_dev_, LoadPairingKey(kEncryptedPk))
      .WillOnce(Return(true));

  EXPECT_TRUE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeIncorrectPinWeaverVersion) {
  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_CALL(*mock_pinweaver_manager_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_manager_, GetVersion).WillOnce(ReturnValue(1));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeNoPkPinWeaverFailed) {
  const brillo::Blob kPubX(32, 1);
  const brillo::Blob kPubY(32, 2);
  const brillo::Blob kEncryptedPriv(32, 3);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_CALL(*mock_pinweaver_manager_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_manager_, GetVersion).WillOnce(ReturnValue(2));
  EXPECT_CALL(*mock_cros_dev_, PairingKeyKeygen)
      .WillOnce(ReturnValue(KeygenReply{
          .pub_x = kPubX,
          .pub_y = kPubY,
          .encrypted_private_key = kEncryptedPriv,
      }));
  EXPECT_CALL(*mock_pinweaver_manager_, GeneratePk(kFingerprintAuthChannel, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestCreateCredentialNotReady) {
  const std::string kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedInSuccess) {
  const std::optional<std::string> kNoUser = std::nullopt;
  const std::string kUserId("testuser");
  const std::vector<CrosFpSessionManager::SessionRecord> kRecords{
      {
          .tmpl = VendorTemplate(32, 1),
      },
      {
          .tmpl = VendorTemplate(32, 2),
      }};

  EXPECT_CALL(*mock_session_manager_, GetUser()).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId)).WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(ReturnRef(kRecords));
  for (const auto& record : kRecords) {
    EXPECT_CALL(*mock_cros_dev_, UploadTemplate(record.tmpl))
        .WillOnce(Return(true));
  }
  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedInLoadUserFailed) {
  const std::optional<std::string> kNoUser = std::nullopt;
  const std::string kUserId("testuser");

  EXPECT_CALL(*mock_session_manager_, GetUser()).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId))
      .WillOnce(Return(false));
  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kLocked);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedInUploadFailed) {
  const std::optional<std::string> kNoUser = std::nullopt;
  const std::string kUserId("testuser");
  const std::vector<CrosFpSessionManager::SessionRecord> kRecords{
      {
          .tmpl = VendorTemplate(32, 1),
      },
      {
          .tmpl = VendorTemplate(32, 2),
      }};

  EXPECT_CALL(*mock_session_manager_, GetUser()).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId)).WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(ReturnRef(kRecords));
  EXPECT_CALL(*mock_cros_dev_, UploadTemplate(kRecords[0].tmpl))
      .WillOnce(Return(false));
  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kLocked);
}

TEST_F(CrosFpAuthStackManagerTest, TestSendStatsOnLogin) {
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(mock_metrics_, SendEnrolledFingerCount(2)).WillOnce(Return(true));
  EXPECT_CALL(mock_metrics_, SendFpUnlockEnabled(true)).WillOnce(Return(true));
  EXPECT_TRUE(cros_fp_auth_stack_manager_->SendStatsOnLogin());
}

TEST_F(CrosFpAuthStackManagerTest, TestSendStatsOnLoginNoTemplates) {
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(0));
  EXPECT_CALL(mock_metrics_, SendEnrolledFingerCount(0)).WillOnce(Return(true));
  EXPECT_CALL(mock_metrics_, SendFpUnlockEnabled(false)).WillOnce(Return(true));
  EXPECT_TRUE(cros_fp_auth_stack_manager_->SendStatsOnLogin());
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedOut) {
  const std::string kUserId("testuser");
  EXPECT_CALL(*mock_session_manager_, UnloadUser);
  cros_fp_auth_stack_manager_->OnUserLoggedOut();
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionStartStopSuccessNoUser) {
  const std::string kUserId("testuser");
  const std::optional<std::string> kNoUser = std::nullopt;
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const std::vector<CrosFpSessionManager::SessionRecord> kRecords{
      {
          .tmpl = VendorTemplate(32, 1),
      },
      {
          .tmpl = VendorTemplate(32, 2),
      }};
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(kUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId)).WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(ReturnRef(kRecords));
  for (const auto& record : kRecords) {
    EXPECT_CALL(*mock_cros_dev_, UploadTemplate(record.tmpl))
        .WillOnce(Return(true));
  }
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_TRUE(auth_session);

  // When auth session ends, FP mode will be set to kNone.
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));

  // Stop auth session
  auth_session.RunAndReset();
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionStartStopNoUserFailed) {
  const std::string kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const std::optional<std::string> kNoUser = std::nullopt;
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(kUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId))
      .WillOnce(Return(false));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionSameUserSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(*kUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*this, AuthScanDoneHandler);

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);

  on_mkbp_event_.Run(EC_MKBP_FP_MATCH);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuthDone);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionDifferentUserSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kSecondUserId("fakeuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const std::vector<CrosFpSessionManager::SessionRecord> kNoRecords;
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(kSecondUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, UnloadUser);
  EXPECT_CALL(*mock_session_manager_, LoadUser(kSecondUserId))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecords())
      .WillOnce(ReturnRef(kNoRecords));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionDifferentUserFail) {
  const std::optional<std::string> kUserId("testuser");
  const std::optional<std::string> kNoUserId = std::nullopt;
  const std::string kSecondUserId("fakeuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const std::vector<CrosFpSessionManager::SessionRecord> kNoRecords;
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(kSecondUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  {
    InSequence s;
    EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));
    EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId.value()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  }
  EXPECT_CALL(*mock_session_manager_, GetRecords())
      .WillOnce(ReturnRef(kNoRecords));

  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId.value());
  // Start auth session. Blocked because there is an existing logged-in user.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestDeleteCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kRecordId("record_id");
  const std::vector<CrosFpSessionManager::SessionRecord> kNoRecords;
  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, HasRecordId(kRecordId))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, DeleteRecord(kRecordId))
      .WillOnce(Return(true));
  // Assume there are no more templates after deletion.
  EXPECT_CALL(*mock_session_manager_, GetRecords())
      .WillOnce(ReturnRef(kNoRecords));

  DeleteCredentialRequest request;
  request.set_user_id(kUserId.value());
  request.set_record_id(kRecordId);
  EXPECT_EQ(cros_fp_auth_stack_manager_->DeleteCredential(request).status(),
            DeleteCredentialReply::SUCCESS);
}

TEST_F(CrosFpAuthStackManagerTest, TestDeleteCredentialNonExisting) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kRecordId("record_id");
  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, HasRecordId(kRecordId))
      .WillOnce(Return(false));

  DeleteCredentialRequest request;
  request.set_user_id(kUserId.value());
  request.set_record_id(kRecordId);
  EXPECT_EQ(cros_fp_auth_stack_manager_->DeleteCredential(request).status(),
            DeleteCredentialReply::NOT_EXIST);
}

TEST_F(CrosFpAuthStackManagerTest, TestDeleteCredentialFailed) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kRecordId("record_id");
  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, HasRecordId(kRecordId))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, DeleteRecord(kRecordId))
      .WillOnce(Return(false));

  DeleteCredentialRequest request;
  request.set_user_id(kUserId.value());
  request.set_record_id(kRecordId);
  EXPECT_EQ(cros_fp_auth_stack_manager_->DeleteCredential(request).status(),
            DeleteCredentialReply::DELETION_FAILED);
}

TEST_F(CrosFpAuthStackManagerTest, TestDeleteCredentialDifferentUser) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kUserId2("testuser2");
  const std::string kRecordId("record_id");
  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_,
              DeleteNotLoadedRecord(kUserId2, kRecordId))
      .WillOnce(Return(true));

  DeleteCredentialRequest request;
  request.set_user_id(kUserId2);
  request.set_record_id(kRecordId);
  EXPECT_EQ(cros_fp_auth_stack_manager_->DeleteCredential(request).status(),
            DeleteCredentialReply::SUCCESS);
}

TEST_F(CrosFpAuthStackManagerTest, TestMaintenanceTimerTooShort) {
  EXPECT_CALL(*mock_cros_dev_, GetFpMode).Times(0);
  task_environment_.FastForwardBy(base::Hours(12));
}

TEST_F(CrosFpAuthStackManagerTest, TestMaintenanceTimerOnce) {
  constexpr int kNumDeadPixels = 1;

  EXPECT_CALL(*mock_cros_dev_, GetFpMode)
      .WillOnce(Return(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(mock_metrics_, SendDeadPixelCount(kNumDeadPixels)).Times(1);
  EXPECT_CALL(*mock_cros_dev_, DeadPixelCount)
      .WillOnce(testing::Return(kNumDeadPixels));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kSensorMaintenance)))
      .Times(1);
  task_environment_.FastForwardBy(base::Days(1));
}

TEST_F(CrosFpAuthStackManagerTest, TestOnMaintenanceTimerRescheduled) {
  constexpr int kNumDeadPixels = 1;

  EXPECT_CALL(*mock_cros_dev_, GetFpMode)
      .WillOnce(Return(ec::FpMode(Mode::kEnrollSession)));
  task_environment_.FastForwardBy(base::Days(1));

  EXPECT_CALL(*mock_cros_dev_, GetFpMode)
      .WillOnce(Return(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(mock_metrics_, SendDeadPixelCount(kNumDeadPixels)).Times(1);
  EXPECT_CALL(*mock_cros_dev_, DeadPixelCount)
      .WillOnce(testing::Return(kNumDeadPixels));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kSensorMaintenance)))
      .Times(1);
  task_environment_.FastForwardBy(base::Minutes(10));
}

TEST_F(CrosFpAuthStackManagerTest, UpdateDirtyTemplates) {
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const RecordMetadata kMetadata{.record_id = "record"};
  const brillo::Blob kTemplate(10, 1);

  SetUpWithInitialState(State::kAuthDone,
                        EC_MKBP_FP_MATCH | EC_MKBP_FP_ERR_MATCH_YES_UPDATED);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  ON_CALL(*mock_session_manager_, GetRecordMetadata)
      .WillByDefault(Return(kMetadata));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(0, kPubInX, kPubInY))
      .WillOnce(Return(GetSecretReply{
          .encrypted_secret = kEncryptedSecret,
          .iv = kSecretIv,
          .pk_out_x = kPubOutX,
          .pk_out_y = kPubOutY,
      }));
  EXPECT_CALL(*mock_cros_dev_, GetDirtyMap)
      .WillOnce(Return(std::bitset<32>("1010")));
  ON_CALL(*mock_cros_dev_, GetTemplate).WillByDefault([&kTemplate](int idx) {
    return std::make_unique<VendorTemplate>(kTemplate);
  });
  EXPECT_CALL(*mock_session_manager_, UpdateRecord)
      .Times(2)
      .WillRepeatedly(Return(true));

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::SUCCESS);
  EXPECT_EQ(reply.scan_result(), ScanResult::SCAN_RESULT_SUCCESS);
  EXPECT_EQ(reply.encrypted_secret(), BlobToString(kEncryptedSecret));
  EXPECT_EQ(reply.pub().x(), BlobToString(kPubOutX));
  EXPECT_EQ(reply.pub().y(), BlobToString(kPubOutY));
  EXPECT_EQ(reply.record_id(), "record");

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerTest, TestListLegacyRecordsNoUser) {
  const std::optional<std::string> kNoUserId = std::nullopt;

  EXPECT_CALL(*mock_legacy_session_manager_, GetUser)
      .WillOnce(ReturnRef(kNoUserId));
  EXPECT_CALL(*mock_legacy_session_manager_, GetRecords).Times(0);

  ListLegacyRecordsReply reply =
      cros_fp_auth_stack_manager_->ListLegacyRecords();
  EXPECT_EQ(reply.status(), ListLegacyRecordsReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerTest, TestListLegacyRecordsSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::vector<CrosFpSessionManager::SessionRecord> kRecords{
      {
          .record_metadata = {.record_id = "record1", .label = "finger1"},
          .tmpl = VendorTemplate(32, 1),
      },
      {
          .record_metadata = {.record_id = "record2", .label = "finger2"},
          .tmpl = VendorTemplate(32, 2),
      }};

  EXPECT_CALL(*mock_legacy_session_manager_, GetUser)
      .WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_legacy_session_manager_, GetRecords)
      .WillOnce(ReturnRef(kRecords));

  ListLegacyRecordsReply reply =
      cros_fp_auth_stack_manager_->ListLegacyRecords();
  ASSERT_EQ(reply.status(), ListLegacyRecordsReply::SUCCESS);
  ASSERT_EQ(reply.legacy_records_size(), 2);
  EXPECT_EQ(reply.legacy_records(0).legacy_record_id(), "record1");
  EXPECT_EQ(reply.legacy_records(0).label(), "finger1");
  EXPECT_EQ(reply.legacy_records(1).legacy_record_id(), "record2");
  EXPECT_EQ(reply.legacy_records(1).label(), "finger2");
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollLegacyTemplateSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kLegacyRecordId("legacy_record");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const CrosFpSessionManager::SessionRecord kRecord{.tmpl =
                                                        VendorTemplate(32, 1)};

  auto request = MakeEnrollLegacyTemplateRequest(
      kLegacyRecordId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_legacy_session_manager_, GetUser)
      .WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_legacy_session_manager_, GetRecordWithId(kLegacyRecordId))
      .WillOnce(Return(kRecord));
  // Expect biod will check if there is space for a new template.
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).WillOnce(Return(3));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, MigrateLegacyTemplate(*kUserId, _))
      .WillOnce(Return(true));

  bool success = false;
  cros_fp_auth_stack_manager_->EnrollLegacyTemplate(
      request,
      base::BindOnce([](bool* success, bool s) { *success = s; }, &success));
  EXPECT_TRUE(success);
}

TEST_F(CrosFpAuthStackManagerTest, TestEnrollLegacyTemplateNoUser) {
  const std::optional<std::string> kNoUserId = std::nullopt;
  const std::string kLegacyRecordId("legacy_record");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);

  auto request = MakeEnrollLegacyTemplateRequest(
      kLegacyRecordId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_legacy_session_manager_, GetUser)
      .WillOnce(ReturnRef(kNoUserId));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).Times(0);
  EXPECT_CALL(*mock_legacy_session_manager_, GetRecordWithId).Times(0);
  EXPECT_CALL(*mock_cros_dev_, MaxTemplateCount).Times(0);
  EXPECT_CALL(*mock_cros_dev_, SetNonceContext).Times(0);
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates).Times(0);
  EXPECT_CALL(*mock_cros_dev_, MigrateLegacyTemplate).Times(0);

  bool success = true;
  cros_fp_auth_stack_manager_->EnrollLegacyTemplate(
      request,
      base::BindOnce([](bool* success, bool s) { *success = s; }, &success));
  EXPECT_FALSE(success);
}

class CrosFpAuthStackManagerInitiallyEnrollDoneTest
    : public CrosFpAuthStackManagerTest {
 public:
  void SetUp() override { SetUpWithInitialState(State::kEnrollDone); }
};

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
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

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  ASSERT_EQ(reply.status(), CreateCredentialReply::SUCCESS);
  EXPECT_EQ(reply.encrypted_secret(), BlobToString(kEncryptedSecret));
  EXPECT_EQ(reply.iv(), BlobToString(kSecretIv));
  EXPECT_EQ(reply.pub().x(), BlobToString(kPubOutX));
  EXPECT_EQ(reply.pub().y(), BlobToString(kPubOutY));
  EXPECT_FALSE(reply.record_id().empty());

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialNoUser) {
  const std::optional<std::string> kNoUserId = std::nullopt;
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialGetTemplateFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1)).Times(1);

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::NO_TEMPLATE);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialGetSecretFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kTemplate(10, 8);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, GetTemplate(-1))
      .WillOnce(Return(ByMove(std::make_unique<VendorTemplate>(kTemplate))));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(-1, kPubInX, kPubInY))
      .WillOnce(Return(std::nullopt));

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::NO_SECRET);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialPersistRecordFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
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

  auto request = MakeCreateCredentialRequest(kPubInX, kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::CREATE_RECORD_FAILED);
}

class CrosFpAuthStackManagerInitiallyWaitForFingerUp
    : public CrosFpAuthStackManagerTest {
 public:
  void SetUp() override { SetUpWithInitialState(State::kWaitForFingerUp); }
};

TEST_F(CrosFpAuthStackManagerInitiallyWaitForFingerUp,
       TestAuthSessionSameUserSuccessDuringWaitForFingerUp) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(*kUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  cros_fp_auth_stack_manager_peer_->RequestFingerUp();

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(),
            State::kAuthWaitForFingerUp);

  // Finger down event should be ignored here.
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_DOWN);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(),
            State::kAuthWaitForFingerUp);

  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);

  EXPECT_CALL(*this, AuthScanDoneHandler);
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));

  // Finger down after lifting the finger first should complete the auth.
  on_mkbp_event_.Run(EC_MKBP_FP_MATCH);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuthDone);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionMatchModeFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  AuthStackManager::Session auth_session;

  auto request = MakeStartAuthSessionRequest(*kUserId, kGscNonce,
                                             kEncryptedLabelSeed, kLabelSeedIv);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, UnlockTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(false));

  // Auth session should fail to start when FPMCU refuses to set finger down
  // mode.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(request);
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthenticateCredentialNotReady) {
  const std::string kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::INCORRECT_STATE);
}

class CrosFpAuthStackManagerInitiallyAuthDone
    : public CrosFpAuthStackManagerTest {
 public:
  void SetUp() override {
    SetUpWithInitialState(State::kAuthDone,
                          EC_MKBP_FP_MATCH | EC_MKBP_FP_ERR_MATCH_YES);
  }
};

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const RecordMetadata kMetadata{.record_id = "record1"};

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
      .WillOnce(Return(kMetadata));
  EXPECT_CALL(*mock_cros_dev_,
              GetPositiveMatchSecretWithPubkey(0, kPubInX, kPubInY))
      .WillOnce(Return(GetSecretReply{
          .encrypted_secret = kEncryptedSecret,
          .iv = kSecretIv,
          .pk_out_x = kPubOutX,
          .pk_out_y = kPubOutY,
      }));

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  ASSERT_EQ(reply.status(), AuthenticateCredentialReply::SUCCESS);
  EXPECT_EQ(reply.encrypted_secret(), BlobToString(kEncryptedSecret));
  EXPECT_EQ(reply.iv(), BlobToString(kSecretIv));
  EXPECT_EQ(reply.pub().x(), BlobToString(kPubOutX));
  EXPECT_EQ(reply.pub().y(), BlobToString(kPubOutY));
  EXPECT_EQ(reply.record_id(), "record1");

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialGetMetadataFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
      .WillOnce(Return(std::nullopt));

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::NO_TEMPLATES);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialGetSecretFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const RecordMetadata kMetadata{.record_id = "record1"};

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
      .WillOnce(Return(kMetadata));
  EXPECT_CALL(*mock_cros_dev_, GetPositiveMatchSecretWithPubkey)
      .WillOnce(Return(std::nullopt));

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::NO_SECRET);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone, PowerButtonEvent) {
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_power_button_filter_, ShouldFilterFingerprintMatch)
      .WillOnce(Return(true));

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::SUCCESS);
  EXPECT_EQ(reply.scan_result(), ScanResult::SCAN_RESULT_POWER_BUTTON_PRESSED);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

struct AuthScanResultTestParam {
  uint32_t event;
  AuthenticateCredentialReply::AuthenticateCredentialStatus status;
  ScanResult scan_result;
};

class CrosFpAuthStackManagerAuthScanResultTest
    : public CrosFpAuthStackManagerTest,
      public ::testing::WithParamInterface<AuthScanResultTestParam> {
 public:
  void SetUp() override {
    SetUpWithInitialState(State::kAuthDone,
                          EC_MKBP_FP_MATCH | GetParam().event);
  }
};

TEST_P(CrosFpAuthStackManagerAuthScanResultTest, ScanResult) {
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const RecordMetadata kMetadata{.record_id = "record1"};
  AuthScanResultTestParam param = GetParam();

  ON_CALL(*mock_cros_dev_, GetDirtyMap).WillByDefault(Return(std::nullopt));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  if (param.status == AuthenticateCredentialReply::SUCCESS &&
      param.scan_result == ScanResult::SCAN_RESULT_SUCCESS) {
    EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
        .WillOnce(Return(kMetadata));
    EXPECT_CALL(*mock_cros_dev_,
                GetPositiveMatchSecretWithPubkey(0, kPubInX, kPubInY))
        .WillOnce(Return(GetSecretReply{
            .encrypted_secret = kEncryptedSecret,
            .iv = kSecretIv,
            .pk_out_x = kPubOutX,
            .pk_out_y = kPubOutY,
        }));
  }

  auto request = MakeAuthenticateCredentialRequest(kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  ASSERT_EQ(reply.status(), param.status);
  if (param.status == AuthenticateCredentialReply::SUCCESS) {
    ASSERT_EQ(reply.scan_result(), param.scan_result);
    if (param.scan_result == ScanResult::SCAN_RESULT_SUCCESS) {
      EXPECT_EQ(reply.encrypted_secret(), BlobToString(kEncryptedSecret));
      EXPECT_EQ(reply.pub().x(), BlobToString(kPubOutX));
      EXPECT_EQ(reply.pub().y(), BlobToString(kPubOutY));
      EXPECT_EQ(reply.record_id(), "record1");
    }
  }

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

INSTANTIATE_TEST_SUITE_P(
    CrosFpAuthStackManagerAuthScanResult,
    CrosFpAuthStackManagerAuthScanResultTest,
    ::testing::Values(
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES,
            .status = AuthenticateCredentialReply::NO_TEMPLATES,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL,
            .status = AuthenticateCredentialReply::INTERNAL_ERROR,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_NO,
            .status = AuthenticateCredentialReply::SUCCESS,
            .scan_result = ScanResult::SCAN_RESULT_NO_MATCH,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_YES,
            .status = AuthenticateCredentialReply::SUCCESS,
            .scan_result = ScanResult::SCAN_RESULT_SUCCESS,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_YES_UPDATED,
            .status = AuthenticateCredentialReply::SUCCESS,
            .scan_result = ScanResult::SCAN_RESULT_SUCCESS,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED,
            .status = AuthenticateCredentialReply::SUCCESS,
            .scan_result = ScanResult::SCAN_RESULT_SUCCESS,
        },
        AuthScanResultTestParam{
            .event = EC_MKBP_FP_ERR_MATCH_NO_LOW_QUALITY,
            .status = AuthenticateCredentialReply::SUCCESS,
            .scan_result = ScanResult::SCAN_RESULT_INSUFFICIENT,
        },
        AuthScanResultTestParam{
            .event = 15,
            .status = AuthenticateCredentialReply::INTERNAL_ERROR,
        }));

}  // namespace biod
