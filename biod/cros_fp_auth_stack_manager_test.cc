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
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/error/testing_helper.h>

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

using PinWeaverEccPoint = hwsec::PinWeaverFrontend::PinWeaverEccPoint;

using hwsec::TPMError;
using hwsec::TPMRetryAction;

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

constexpr base::TimeDelta kMatchTimeout = base::Seconds(2);

constexpr uint8_t kCrosFpAuthChannel = 0;

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

AuthenticateCredentialRequest MakeAuthenticateCredentialRequest(
    const std::string& user_id,
    const brillo::Blob& gsc_nonce,
    const brillo::Blob& encrypted_label_seed,
    const brillo::Blob& iv,
    const brillo::Blob& pub_x,
    const brillo::Blob& pub_y) {
  AuthenticateCredentialRequest request;
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

  void SetUpWithInitialState(State state = State::kNone) {
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

    auto mock_pinweaver = std::make_unique<hwsec::MockPinWeaverFrontend>();
    mock_pinweaver_ = mock_pinweaver.get();

    auto cros_fp_auth_stack_manager = std::make_unique<CrosFpAuthStackManager>(
        PowerButtonFilter::Create(mock_bus), std::move(mock_cros_dev),
        &mock_metrics_, std::move(mock_session_manager),
        std::move(mock_pk_storage), std::move(mock_pinweaver), state);

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

  MOCK_METHOD(void, AuthScanDoneHandler, (brillo::Blob auth_nonce));

 protected:
  MOCK_METHOD(void,
              EnrollScanDoneHandler,
              (ScanResult, const EnrollStatus&, brillo::Blob));

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::optional<CrosFpAuthStackManagerPeer> cros_fp_auth_stack_manager_peer_;
  metrics::MockBiodMetrics mock_metrics_;
  CrosFpAuthStackManager* cros_fp_auth_stack_manager_;
  MockPairingKeyStorage* mock_pk_storage_;
  MockCrosFpSessionManager* mock_session_manager_;
  MockCrosFpDevice* mock_cros_dev_;
  hwsec::MockPinWeaverFrontend* mock_pinweaver_;
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
  const brillo::Blob kPubX(32, 1);
  const brillo::Blob kPubY(32, 2);
  const brillo::Blob kEncryptedPriv(32, 3);
  const brillo::Blob kEncryptedPk(32, 4);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_CALL(*mock_pinweaver_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_, GetVersion).WillOnce(ReturnValue(2));
  EXPECT_CALL(*mock_cros_dev_, PairingKeyKeygen)
      .WillOnce(ReturnValue(KeygenReply{
          .pub_x = kPubX,
          .pub_y = kPubY,
          .encrypted_private_key = kEncryptedPriv,
      }));
  EXPECT_CALL(*mock_pinweaver_, GeneratePk(kCrosFpAuthChannel, _))
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

  EXPECT_CALL(*mock_pinweaver_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_, GetVersion).WillOnce(ReturnValue(1));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
}

TEST_F(CrosFpAuthStackManagerTest, TestInitializeNoPkPinWeaverFailed) {
  const brillo::Blob kPubX(32, 1);
  const brillo::Blob kPubY(32, 2);
  const brillo::Blob kEncryptedPriv(32, 3);

  EXPECT_CALL(*mock_pk_storage_, PairingKeyExists).WillOnce(Return(false));

  EXPECT_CALL(*mock_pinweaver_, IsEnabled).WillOnce(ReturnValue(true));
  EXPECT_CALL(*mock_pinweaver_, GetVersion).WillOnce(ReturnValue(2));
  EXPECT_CALL(*mock_cros_dev_, PairingKeyKeygen)
      .WillOnce(ReturnValue(KeygenReply{
          .pub_x = kPubX,
          .pub_y = kPubY,
          .encrypted_private_key = kEncryptedPriv,
      }));
  EXPECT_CALL(*mock_pinweaver_, GeneratePk(kCrosFpAuthChannel, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  EXPECT_FALSE(cros_fp_auth_stack_manager_->Initialize());
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
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(Return(kRecords));
  for (int i = 0; i < 2; i++) {
    EXPECT_CALL(*mock_cros_dev_, PreloadTemplate(i, kRecords[i].tmpl))
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
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(Return(kRecords));
  EXPECT_CALL(*mock_cros_dev_, PreloadTemplate(0, kRecords[0].tmpl))
      .WillOnce(Return(false));
  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kLocked);
}

TEST_F(CrosFpAuthStackManagerTest, TestOnUserLoggedOut) {
  const std::string kUserId("testuser");
  EXPECT_CALL(*mock_session_manager_, UnloadUser);
  cros_fp_auth_stack_manager_->OnUserLoggedOut();
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionStartStopSuccessNoUser) {
  const std::string kUserId("testuser");
  const std::optional<std::string> kNoUser = std::nullopt;
  const std::vector<CrosFpSessionManager::SessionRecord> kRecords{
      {
          .tmpl = VendorTemplate(32, 1),
      },
      {
          .tmpl = VendorTemplate(32, 2),
      }};
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerDown)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId)).WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecords).WillOnce(Return(kRecords));
  for (int i = 0; i < 2; i++) {
    EXPECT_CALL(*mock_cros_dev_, PreloadTemplate(i, kRecords[i].tmpl))
        .WillOnce(Return(true));
  }

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kUserId);
  EXPECT_TRUE(auth_session);

  // When auth session ends, FP mode will be set to kNone.
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)))
      .WillOnce(Return(true));

  // Stop auth session
  auth_session.RunAndReset();
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionStartStopNoUserFailed) {
  const std::string kUserId("testuser");
  const std::optional<std::string> kNoUser = std::nullopt;
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUser));
  EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId))
      .WillOnce(Return(false));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kUserId);
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionSameUserSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kAuthNonce(32, 1);
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerDown)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(*mock_cros_dev_, GetNonce).WillOnce(Return(kAuthNonce));
  EXPECT_CALL(*this, AuthScanDoneHandler(kAuthNonce));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kUserId.value());
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_DOWN);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuthDone);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionDifferentUserSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kSecondUserId("fakeuser");
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, UnloadUser);
  EXPECT_CALL(*mock_session_manager_, LoadUser(kSecondUserId))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerDown)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kSecondUserId);
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionDifferentUserFail) {
  const std::optional<std::string> kUserId("testuser");
  const std::optional<std::string> kNoUserId = std::nullopt;
  const std::string kSecondUserId("fakeuser");
  AuthStackManager::Session auth_session;

  {
    InSequence s;
    EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));
    EXPECT_CALL(*mock_session_manager_, LoadUser(kUserId.value()))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  }

  cros_fp_auth_stack_manager_->OnUserLoggedIn(kUserId.value());
  // Start auth session. Blocked because there is an existing logged-in user.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kSecondUserId);
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestDeleteCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kRecordId("record_id");
  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_session_manager_, HasRecordId(kRecordId))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, DeleteRecord(kRecordId))
      .WillOnce(Return(true));
  // Assume there are no more templates after deletion.
  // `PreloadCurrentUserTemplates` is covered in other tests already anyway.
  EXPECT_CALL(*mock_session_manager_, GetRecords())
      .WillOnce(Return(std::vector<CrosFpSessionManager::SessionRecord>()));

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

class CrosFpAuthStackManagerInitiallyEnrollDoneTest
    : public CrosFpAuthStackManagerTest {
 public:
  void SetUp() override { SetUpWithInitialState(State::kEnrollDone); }
};

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

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
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, PreloadTemplate(1, kTemplate))
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

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialPreloadFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

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
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, PreloadTemplate(1, kTemplate))
      .WillOnce(Return(false));

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

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kLocked);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialNoUser) {
  const std::optional<std::string> kUserId("testuser");
  const std::optional<std::string> kNoUserId = std::nullopt;
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUserId));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialIncorrectUser) {
  const std::optional<std::string> kUserId("testuser");
  const std::string kFakeUserId("fakeuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kFakeUserId));

  CreateCredentialRequest request = MakeCreateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  auto reply = cros_fp_auth_stack_manager_->CreateCredential(request);
  EXPECT_EQ(reply.status(), CreateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialSetNonceFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

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

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialGetTemplateFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

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

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialGetSecretFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kTemplate(10, 8);

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

TEST_F(CrosFpAuthStackManagerInitiallyEnrollDoneTest,
       TestCreateCredentialPersistRecordFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const brillo::Blob kTemplate(10, 8);

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

class CrosFpAuthStackManagerInitiallyWaitForFingerUp
    : public CrosFpAuthStackManagerTest {
 public:
  void SetUp() override { SetUpWithInitialState(State::kWaitForFingerUp); }
};

TEST_F(CrosFpAuthStackManagerInitiallyWaitForFingerUp,
       TestAuthSessionSameUserSuccessDuringWaitForFingerUp) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kAuthNonce(32, 1);
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  cros_fp_auth_stack_manager_peer_->RequestFingerUp();

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));

  // Start auth session.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kUserId.value());
  EXPECT_TRUE(auth_session);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(),
            State::kAuthWaitForFingerUp);

  // Finger down event should be ignored here.
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_DOWN);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(),
            State::kAuthWaitForFingerUp);

  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerDown)))
      .WillOnce(Return(true));
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuth);

  EXPECT_CALL(*this, AuthScanDoneHandler(kAuthNonce));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kNone)));
  EXPECT_CALL(*mock_cros_dev_, GetNonce).WillOnce(Return(kAuthNonce));

  // Finger down after lifting the finger first should complete the auth.
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_DOWN);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kAuthDone);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthSessionFingerDownModeFailed) {
  const std::optional<std::string> kUserId("testuser");
  AuthStackManager::Session auth_session;

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerDown)))
      .WillOnce(Return(false));

  // Auth session should fail to start when FPMCU refuses to set finger down
  // mode.
  auth_session = cros_fp_auth_stack_manager_->StartAuthSession(kUserId.value());
  EXPECT_FALSE(auth_session);
}

TEST_F(CrosFpAuthStackManagerTest, TestAuthenticateCredentialNotReady) {
  const std::string kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX, kPubInY);

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
  void SetUp() override { SetUpWithInitialState(State::kAuthDone); }
};

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialSuccess) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const RecordMetadata kMetadata{.record_id = "record1"};

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
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

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  // There might be other unrelated signals before the match signal. Test
  // whether we handled the callbacks correctly.
  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  on_mkbp_event_.Run(EC_MKBP_FP_MATCH | EC_MKBP_FP_ERR_MATCH_YES);

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
       TestAuthenticateCredentialNoUser) {
  const std::string kUserId("testuser");
  const std::optional<std::string> kNoUser = std::nullopt;
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kNoUser));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId, kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX, kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::INCORRECT_STATE);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialSetNonceContextFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_, SetNonceContext).WillOnce(Return(false));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::SET_NONCE_FAILED);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialReloadTemplatesFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(false));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));

  EXPECT_EQ(reply.status(),
            AuthenticateCredentialReply::UPLOAD_TEMPLATES_FAILED);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialTimeout) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  task_environment_.FastForwardBy(kMatchTimeout);

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::MATCH_FAILED);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialGetMetadataFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
      .WillOnce(Return(std::nullopt));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  on_mkbp_event_.Run(EC_MKBP_FP_MATCH | EC_MKBP_FP_ERR_MATCH_YES);

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::NO_TEMPLATES);

  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kWaitForFingerUp);

  on_mkbp_event_.Run(EC_MKBP_FP_FINGER_UP);
  EXPECT_EQ(cros_fp_auth_stack_manager_->GetState(), State::kNone);
}

TEST_F(CrosFpAuthStackManagerInitiallyAuthDone,
       TestAuthenticateCredentialGetSecretFailed) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const RecordMetadata kMetadata{.record_id = "record1"};

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kFingerUp)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetRecordMetadata(0))
      .WillOnce(Return(kMetadata));
  EXPECT_CALL(*mock_cros_dev_, GetPositiveMatchSecretWithPubkey)
      .WillOnce(Return(std::nullopt));

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  on_mkbp_event_.Run(EC_MKBP_FP_MATCH | EC_MKBP_FP_ERR_MATCH_YES);

  EXPECT_EQ(reply.status(), AuthenticateCredentialReply::NO_SECRET);

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
  void SetUp() override { SetUpWithInitialState(State::kAuthDone); }
};

TEST_P(CrosFpAuthStackManagerAuthScanResultTest, ScanResult) {
  const std::optional<std::string> kUserId("testuser");
  const brillo::Blob kGscNonce(32, 1);
  const brillo::Blob kEncryptedLabelSeed(32, 2), kLabelSeedIv(16, 2);
  const brillo::Blob kPubInX(32, 3), kPubInY(32, 4);
  const brillo::Blob kEncryptedSecret(32, 5), kSecretIv(16, 5);
  const brillo::Blob kPubOutX(32, 6), kPubOutY(32, 7);
  const RecordMetadata kMetadata{.record_id = "record1"};
  AuthScanResultTestParam param = GetParam();

  EXPECT_CALL(*mock_session_manager_, GetUser).WillOnce(ReturnRef(kUserId));
  EXPECT_CALL(*mock_cros_dev_,
              SetNonceContext(kGscNonce, kEncryptedLabelSeed, kLabelSeedIv))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_session_manager_, GetNumOfTemplates).WillOnce(Return(2));
  EXPECT_CALL(*mock_cros_dev_, ReloadTemplates(2)).WillOnce(Return(true));
  EXPECT_CALL(*mock_cros_dev_, SetFpMode(ec::FpMode(Mode::kMatch)))
      .WillOnce(Return(true));
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

  AuthenticateCredentialRequest request = MakeAuthenticateCredentialRequest(
      kUserId.value(), kGscNonce, kEncryptedLabelSeed, kLabelSeedIv, kPubInX,
      kPubInY);

  AuthenticateCredentialReply reply;
  cros_fp_auth_stack_manager_->AuthenticateCredential(
      request, base::BindOnce([](AuthenticateCredentialReply* reply,
                                 AuthenticateCredentialReply r) { *reply = r; },
                              &reply));
  on_mkbp_event_.Run(EC_MKBP_FP_MATCH | param.event);

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
