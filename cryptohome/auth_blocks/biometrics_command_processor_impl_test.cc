// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/biometrics_command_processor_impl.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/functional/callback.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/repeating_test_future.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <biod/biod_proxy/mock_auth_stack_manager_proxy_base.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {
namespace {

using base::test::RepeatingTestFuture;
using base::test::TestFuture;
using hwsec_foundation::error::testing::IsOkAnd;

using ::testing::_;
using ::testing::Field;
using ::testing::Optional;
using ::testing::SaveArg;
using ::testing::SizeIs;

// As the point needs to be valid, the point is pre-generated.
constexpr char kPubPointXHex[] =
    "78D184E439FD4EC5BADC5431C8A6DD8EC039F945E7AD9DEDC5166BEF390E9AFD";
constexpr char kPubPointYHex[] =
    "4E411B61F1B48601ED3A218E4EE6075A3053130E6F25BBFF7FE08BB6D3EC6BF6";

constexpr char kFakeRecordId[] = "fake_record_id";

biod::EnrollScanDone ConstructEnrollScanDone(biod::ScanResult scan_result,
                                             int percent_complete) {
  biod::EnrollScanDone ret;
  ret.set_scan_result(scan_result);
  ret.set_done(percent_complete == 100);
  ret.set_percent_complete(percent_complete);
  return ret;
}

biod::CreateCredentialReply ConstructCreateCredentialReply(
    biod::CreateCredentialReply::CreateCredentialStatus create_status) {
  const std::string kFakeEncryptedSecret(32, 1), kFakeIv(16, 2);

  biod::CreateCredentialReply reply;
  reply.set_status(create_status);
  if (create_status != biod::CreateCredentialReply::SUCCESS) {
    return reply;
  }
  reply.set_encrypted_secret(kFakeEncryptedSecret);
  reply.set_iv(kFakeIv);
  brillo::Blob x, y;
  base::HexStringToBytes(kPubPointXHex, &x);
  base::HexStringToBytes(kPubPointYHex, &y);
  reply.mutable_pub()->set_x(brillo::BlobToString(x));
  reply.mutable_pub()->set_y(brillo::BlobToString(y));
  reply.set_record_id(kFakeRecordId);
  return reply;
}

biod::AuthenticateCredentialReply ConstructAuthenticateCredentialReply(
    biod::AuthenticateCredentialReply::AuthenticateCredentialStatus auth_status,
    std::optional<biod::ScanResult> scan_result) {
  const std::string kFakeEncryptedSecret(32, 1), kFakeIv(16, 2);

  biod::AuthenticateCredentialReply reply;
  reply.set_status(auth_status);
  if (auth_status != biod::AuthenticateCredentialReply::SUCCESS) {
    return reply;
  }
  reply.set_scan_result(*scan_result);
  if (*scan_result != biod::SCAN_RESULT_SUCCESS) {
    return reply;
  }
  reply.set_encrypted_secret(kFakeEncryptedSecret);
  reply.set_iv(kFakeIv);
  brillo::Blob x, y;
  base::HexStringToBytes(kPubPointXHex, &x);
  base::HexStringToBytes(kPubPointYHex, &y);
  reply.mutable_pub()->set_x(brillo::BlobToString(x));
  reply.mutable_pub()->set_y(brillo::BlobToString(y));
  reply.set_record_id(kFakeRecordId);
  return reply;
}

// Base test fixture which sets up the task environment.
class BaseTestFixture : public ::testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();
};

class BiometricsCommandProcessorImplTest : public BaseTestFixture {
 public:
  void SetUp() override {
    auto mock_proxy = std::make_unique<biod::MockAuthStackManagerProxyBase>();
    mock_proxy_ = mock_proxy.get();
    EXPECT_CALL(*mock_proxy_, ConnectToEnrollScanDoneSignal(_, _))
        .WillOnce([&](auto&& callback, auto&& on_connected_callback) {
          enroll_callback_ = callback;
          enroll_connected_callback_ = std::move(on_connected_callback);
        });
    EXPECT_CALL(*mock_proxy_, ConnectToAuthScanDoneSignal(_, _))
        .WillOnce([&](auto&& callback, auto&& on_connected_callback) {
          auth_callback_ = callback;
          auth_connected_callback_ = std::move(on_connected_callback);
        });
    EXPECT_CALL(*mock_proxy_, ConnectToSessionFailedSignal(_, _))
        .WillOnce([&](auto&& callback, auto&& on_connected_callback) {
          session_failed_callback_ = callback;
          session_failed_connected_callback_ = std::move(on_connected_callback);
        });
    processor_ =
        std::make_unique<BiometricsCommandProcessorImpl>(std::move(mock_proxy));
  }

 protected:
  const ObfuscatedUsername kFakeUserId{"fake"};

  void EmitEnrollEvent(biod::EnrollScanDone enroll_scan) {
    dbus::Signal enroll_scan_done_signal(
        biod::kBiometricsManagerInterface,
        biod::kBiometricsManagerEnrollScanDoneSignal);
    dbus::MessageWriter writer(&enroll_scan_done_signal);
    writer.AppendProtoAsArrayOfBytes(enroll_scan);
    enroll_callback_.Run(&enroll_scan_done_signal);
  }

  void EmitAuthEvent() {
    dbus::Signal auth_scan_done_signal(
        biod::kBiometricsManagerInterface,
        biod::kBiometricsManagerAuthScanDoneSignal);
    dbus::MessageWriter writer(&auth_scan_done_signal);
    writer.AppendProtoAsArrayOfBytes(biod::AuthScanDone{});
    auth_callback_.Run(&auth_scan_done_signal);
  }

  void EmitSessionFailedEvent() {
    dbus::Signal session_failed_signal(
        biod::kBiometricsManagerInterface,
        biod::kBiometricsManagerSessionFailedSignal);
    session_failed_callback_.Run(&session_failed_signal);
  }

  base::RepeatingCallback<void(dbus::Signal*)> enroll_callback_;
  base::OnceCallback<void(const std::string&, const std::string&, bool success)>
      enroll_connected_callback_;
  base::RepeatingCallback<void(dbus::Signal*)> auth_callback_;
  base::OnceCallback<void(const std::string&, const std::string&, bool success)>
      auth_connected_callback_;
  base::RepeatingCallback<void(dbus::Signal*)> session_failed_callback_;
  base::OnceCallback<void(const std::string&, const std::string&, bool success)>
      session_failed_connected_callback_;
  biod::MockAuthStackManagerProxyBase* mock_proxy_;
  std::unique_ptr<BiometricsCommandProcessorImpl> processor_;
};

TEST_F(BiometricsCommandProcessorImplTest, IsReady) {
  EXPECT_EQ(processor_->IsReady(), false);
  std::move(enroll_connected_callback_).Run("", "", true);
  EXPECT_EQ(processor_->IsReady(), false);
  std::move(auth_connected_callback_).Run("", "", true);
  EXPECT_EQ(processor_->IsReady(), false);
  std::move(session_failed_connected_callback_).Run("", "", true);
  EXPECT_EQ(processor_->IsReady(), true);
}

TEST_F(BiometricsCommandProcessorImplTest, ConnectToSignalFailed) {
  // If one of the signal connection failed, the processor shouldn't be in the
  // ready state.
  std::move(enroll_connected_callback_).Run("", "", false);
  std::move(auth_connected_callback_).Run("", "", true);
  std::move(session_failed_connected_callback_).Run("", "", true);
  EXPECT_EQ(processor_->IsReady(), false);
}

TEST_F(BiometricsCommandProcessorImplTest, GetNonceEmptyNonce) {
  biod::GetNonceReply reply;
  EXPECT_CALL(*mock_proxy_, GetNonce(_)).WillOnce([&reply](auto&& callback) {
    std::move(callback).Run(reply);
  });

  TestFuture<std::optional<brillo::Blob>> future;
  processor_->GetNonce(future.GetCallback());
  EXPECT_FALSE(future.Get().has_value());
}

TEST_F(BiometricsCommandProcessorImplTest, GetNonceEmptyReply) {
  EXPECT_CALL(*mock_proxy_, GetNonce(_)).WillOnce([](auto&& callback) {
    std::move(callback).Run(std::nullopt);
  });

  TestFuture<std::optional<brillo::Blob>> future;
  processor_->GetNonce(future.GetCallback());
  EXPECT_FALSE(future.Get().has_value());
}

TEST_F(BiometricsCommandProcessorImplTest, StartEndEnrollSession) {
  const BiometricsCommandProcessor::OperationInput kFakeInput{
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };
  EXPECT_CALL(*mock_proxy_, StartEnrollSession(_, _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });

  TestFuture<bool> result;
  processor_->StartEnrollSession(kFakeInput, result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_TRUE(result.Get());

  EXPECT_CALL(*mock_proxy_, EndEnrollSession).Times(1);
  processor_->EndEnrollSession();
}

TEST_F(BiometricsCommandProcessorImplTest, StartEndAuthenticateSession) {
  const BiometricsCommandProcessor::OperationInput kFakeInput{
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };
  EXPECT_CALL(*mock_proxy_, StartAuthSession(_, _))
      .WillOnce([](auto&&, auto&& callback) { std::move(callback).Run(true); });

  TestFuture<bool> result;
  processor_->StartAuthenticateSession(kFakeUserId, kFakeInput,
                                       result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_TRUE(result.Get());

  EXPECT_CALL(*mock_proxy_, EndAuthSession).Times(1);
  processor_->EndAuthenticateSession();
}

TEST_F(BiometricsCommandProcessorImplTest, ReceiveEnrollSignal) {
  RepeatingTestFuture<user_data_auth::AuthEnrollmentProgress> enroll_signals;
  processor_->SetEnrollScanDoneCallback(enroll_signals.GetCallback());

  EmitEnrollEvent(ConstructEnrollScanDone(biod::SCAN_RESULT_PARTIAL, 50));
  ASSERT_FALSE(enroll_signals.IsEmpty());
  auto progress = enroll_signals.Take();
  EXPECT_EQ(progress.scan_result().fingerprint_result(),
            user_data_auth::FINGERPRINT_SCAN_RESULT_PARTIAL);
  EXPECT_FALSE(progress.done());
  EXPECT_EQ(progress.fingerprint_progress().percent_complete(), 50);

  EmitEnrollEvent(ConstructEnrollScanDone(biod::SCAN_RESULT_SUCCESS, 100));
  ASSERT_FALSE(enroll_signals.IsEmpty());
  progress = enroll_signals.Take();
  EXPECT_EQ(progress.scan_result().fingerprint_result(),
            user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS);
  EXPECT_TRUE(progress.done());
  EXPECT_EQ(progress.fingerprint_progress().percent_complete(), 100);
}

TEST_F(BiometricsCommandProcessorImplTest, ReceiveAuthSignal) {
  RepeatingTestFuture<user_data_auth::AuthScanDone> auth_signals;
  processor_->SetAuthScanDoneCallback(auth_signals.GetCallback());

  EmitAuthEvent();
  ASSERT_FALSE(auth_signals.IsEmpty());
  auto scan = auth_signals.Take();
  EXPECT_EQ(scan.scan_result().fingerprint_result(),
            user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS);

  EmitAuthEvent();
  ASSERT_FALSE(auth_signals.IsEmpty());
  scan = auth_signals.Take();
  EXPECT_EQ(scan.scan_result().fingerprint_result(),
            user_data_auth::FINGERPRINT_SCAN_RESULT_SUCCESS);
}

TEST_F(BiometricsCommandProcessorImplTest, ReceiveSessionFailed) {
  bool called = false;
  processor_->SetSessionFailedCallback(
      base::BindLambdaForTesting([&called]() { called = true; }));

  EXPECT_FALSE(called);
  EmitSessionFailedEvent();
  EXPECT_TRUE(called);
}

TEST_F(BiometricsCommandProcessorImplTest, CreateCredential) {
  EXPECT_CALL(*mock_proxy_, CreateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(ConstructCreateCredentialReply(
            biod::CreateCredentialReply::SUCCESS));
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->CreateCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  ASSERT_THAT(
      result.Get(),
      IsOkAnd(
          AllOf(Field(&BiometricsCommandProcessor::OperationOutput::record_id,
                      kFakeRecordId),
                Field(&BiometricsCommandProcessor::OperationOutput::auth_secret,
                      SizeIs(32)),
                Field(&BiometricsCommandProcessor::OperationOutput::auth_pin,
                      SizeIs(32)))));
}

TEST_F(BiometricsCommandProcessorImplTest, CreateCredentialNoReply) {
  EXPECT_CALL(*mock_proxy_, CreateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(std::nullopt);
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->CreateCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_EQ(result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsCommandProcessorImplTest, CreateCredentialFailure) {
  EXPECT_CALL(*mock_proxy_, CreateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(ConstructCreateCredentialReply(
            biod::CreateCredentialReply::INCORRECT_STATE));
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->CreateCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_EQ(result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsCommandProcessorImplTest, MatchCredential) {
  EXPECT_CALL(*mock_proxy_, AuthenticateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(ConstructAuthenticateCredentialReply(
            biod::AuthenticateCredentialReply::SUCCESS,
            biod::SCAN_RESULT_SUCCESS));
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->MatchCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  ASSERT_THAT(
      result.Get(),
      IsOkAnd(
          AllOf(Field(&BiometricsCommandProcessor::OperationOutput::record_id,
                      kFakeRecordId),
                Field(&BiometricsCommandProcessor::OperationOutput::auth_secret,
                      SizeIs(32)),
                Field(&BiometricsCommandProcessor::OperationOutput::auth_pin,
                      SizeIs(32)))));
}

TEST_F(BiometricsCommandProcessorImplTest, MatchCredentialNoReply) {
  const BiometricsCommandProcessor::OperationInput kFakeInput{
      .nonce = brillo::Blob(32, 1),
      .encrypted_label_seed = brillo::Blob(32, 2),
      .iv = brillo::Blob(16, 3),
  };

  EXPECT_CALL(*mock_proxy_, AuthenticateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(std::nullopt);
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->MatchCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_EQ(result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsCommandProcessorImplTest, AuthenticateCredentialFailure) {
  EXPECT_CALL(*mock_proxy_, AuthenticateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(ConstructAuthenticateCredentialReply(
            biod::AuthenticateCredentialReply::INCORRECT_STATE, std::nullopt));
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->MatchCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_EQ(result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_ERROR_INTERNAL);
}

TEST_F(BiometricsCommandProcessorImplTest, AuthenticateCredentialNoMatch) {
  EXPECT_CALL(*mock_proxy_, AuthenticateCredential(_, _))
      .WillOnce([](auto&&, auto&& callback) {
        std::move(callback).Run(ConstructAuthenticateCredentialReply(
            biod::AuthenticateCredentialReply::SUCCESS,
            biod::SCAN_RESULT_INSUFFICIENT));
      });

  TestFuture<CryptohomeStatusOr<BiometricsCommandProcessor::OperationOutput>>
      result;
  processor_->MatchCredential(result.GetCallback());
  ASSERT_TRUE(result.IsReady());
  EXPECT_EQ(result.Get().status()->local_legacy_error(),
            user_data_auth::CRYPTOHOME_ERROR_FINGERPRINT_RETRY_REQUIRED);
}

TEST_F(BiometricsCommandProcessorImplTest, DeleteCredentialSuccess) {
  const std::string kRecordId("record_id");

  EXPECT_CALL(*mock_proxy_, DeleteCredential)
      .WillOnce([](auto&&, auto&& callback) {
        biod::DeleteCredentialReply reply;
        reply.set_status(biod::DeleteCredentialReply::SUCCESS);
        std::move(callback).Run(reply);
      });

  TestFuture<BiometricsCommandProcessor::DeleteResult> result;
  processor_->DeleteCredential(kFakeUserId, kRecordId, result.GetCallback());
  EXPECT_EQ(result.Get(), BiometricsCommandProcessor::DeleteResult::kSuccess);
}

TEST_F(BiometricsCommandProcessorImplTest, DeleteCredentialNotExist) {
  const std::string kRecordId("record_id");

  EXPECT_CALL(*mock_proxy_, DeleteCredential)
      .WillOnce([](auto&&, auto&& callback) {
        biod::DeleteCredentialReply reply;
        reply.set_status(biod::DeleteCredentialReply::NOT_EXIST);
        std::move(callback).Run(reply);
      });

  TestFuture<BiometricsCommandProcessor::DeleteResult> result;
  processor_->DeleteCredential(kFakeUserId, kRecordId, result.GetCallback());
  EXPECT_EQ(result.Get(), BiometricsCommandProcessor::DeleteResult::kNotExist);
}

TEST_F(BiometricsCommandProcessorImplTest, DeleteCredentialFailed) {
  const std::string kRecordId("record_id");

  EXPECT_CALL(*mock_proxy_, DeleteCredential)
      .WillOnce([](auto&&, auto&& callback) {
        biod::DeleteCredentialReply reply;
        reply.set_status(biod::DeleteCredentialReply::DELETION_FAILED);
        std::move(callback).Run(reply);
      });

  TestFuture<BiometricsCommandProcessor::DeleteResult> result;
  processor_->DeleteCredential(kFakeUserId, kRecordId, result.GetCallback());
  EXPECT_EQ(result.Get(), BiometricsCommandProcessor::DeleteResult::kFailed);
}

}  // namespace
}  // namespace cryptohome
