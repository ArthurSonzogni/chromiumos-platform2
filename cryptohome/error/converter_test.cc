// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <utility>

#include <base/optional.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/error.h>

#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

namespace {

using hwsec_foundation::status::MakeStatus;

constexpr int kTestLocation1 = 10001;
constexpr int kTestLocation2 = 10002;
constexpr char kTestSanitizedUsername1[] = "Abcdefghijklmnop1234!@#$%^&*()";

// Note that the RepeatedField field in protobuf for PossibleAction uses int,
// thus the need to for 2 template types.
template <typename T, typename S>
std::set<T> ToStdSet(const ::google::protobuf::RepeatedField<S>& input) {
  std::vector<T> list;
  for (int i = 0; i < input.size(); i++) {
    list.push_back(static_cast<T>(input[i]));
  }
  return std::set<T>(list.begin(), list.end());
}

class ErrorConverterTest : public ::testing::Test {
 public:
  ErrorConverterTest() {}
  ~ErrorConverterTest() override = default;
};

TEST_F(ErrorConverterTest, BasicConversionTest) {
  hwsec::StatusChain<CryptohomeError> err1 = MakeStatus<CryptohomeError>(
      kTestLocation2, ErrorActionSet({ErrorAction::kPowerwash}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);

  user_data_auth::CryptohomeErrorCode ec =
      static_cast<user_data_auth::CryptohomeErrorCode>(
          123451234);  // Intentionally invalid value.
  user_data_auth::CryptohomeErrorInfo info =
      CryptohomeErrorToUserDataAuthError(err1, &ec);
  EXPECT_EQ(ec, user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  EXPECT_EQ(info.error_id(), "10002");
  EXPECT_EQ(info.primary_action(), user_data_auth::PrimaryAction::PRIMARY_NONE);
  ASSERT_EQ(info.possible_actions_size(), 1);
  EXPECT_EQ(info.possible_actions(0),
            user_data_auth::PossibleAction::POSSIBLY_POWERWASH);
}

TEST_F(ErrorConverterTest, Success) {
  hwsec::StatusChain<CryptohomeError> err1;

  user_data_auth::CryptohomeErrorCode ec =
      static_cast<user_data_auth::CryptohomeErrorCode>(
          123451234);  // Intentionally invalid value.
  user_data_auth::CryptohomeErrorInfo info =
      CryptohomeErrorToUserDataAuthError(err1, &ec);
  EXPECT_EQ(ec, user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(info.error_id(), "");
  EXPECT_EQ(info.primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
  EXPECT_EQ(info.possible_actions_size(), 0);
}

TEST_F(ErrorConverterTest, WrappedPossibleAction) {
  hwsec::StatusChain<CryptohomeError> err1 = MakeStatus<CryptohomeError>(
      kTestLocation2, ErrorActionSet({ErrorAction::kPowerwash}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);

  hwsec::StatusChain<CryptohomeError> err2 =
      MakeStatus<CryptohomeError>(kTestLocation1,
                                  ErrorActionSet({ErrorAction::kReboot}))
          .Wrap(std::move(err1));

  user_data_auth::CryptohomeErrorCode ec =
      static_cast<user_data_auth::CryptohomeErrorCode>(
          123451234);  // Intentionally invalid value.
  user_data_auth::CryptohomeErrorInfo info =
      CryptohomeErrorToUserDataAuthError(err2, &ec);
  EXPECT_EQ(ec, user_data_auth::CryptohomeErrorCode::
                    CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  EXPECT_EQ(info.error_id(), "10001-10002");
  EXPECT_EQ(info.primary_action(), user_data_auth::PrimaryAction::PRIMARY_NONE);
  EXPECT_EQ(ToStdSet<user_data_auth::PossibleAction>(info.possible_actions()),
            std::set<user_data_auth::PossibleAction>(
                {user_data_auth::PossibleAction::POSSIBLY_POWERWASH,
                 user_data_auth::PossibleAction::POSSIBLY_REBOOT}));
}

TEST_F(ErrorConverterTest, WrappedPrimaryAction) {
  hwsec::StatusChain<CryptohomeError> err1 = MakeStatus<CryptohomeError>(
      kTestLocation2, ErrorActionSet({ErrorAction::kTpmUpdateRequired}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);

  hwsec::StatusChain<CryptohomeError> err2 =
      MakeStatus<CryptohomeError>(kTestLocation1,
                                  ErrorActionSet({ErrorAction::kReboot}))
          .Wrap(std::move(err1));

  user_data_auth::CryptohomeErrorCode ec;
  user_data_auth::CryptohomeErrorInfo info =
      CryptohomeErrorToUserDataAuthError(err2, &ec);

  EXPECT_EQ(info.primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_TPM_UDPATE_REQUIRED);
  EXPECT_EQ(info.possible_actions_size(), 0);
}

TEST_F(ErrorConverterTest, ReplyWithErrorPrimary) {
  // Prepare the callback.
  bool reply_received = false;
  user_data_auth::MountReply received_reply;
  auto cb = base::BindOnce(
      [](bool* reply_received_ptr,
         user_data_auth::MountReply* received_reply_ptr,
         const user_data_auth::MountReply& cb_reply) {
        ASSERT_FALSE(*reply_received_ptr);
        *reply_received_ptr = true;
        received_reply_ptr->CopyFrom(cb_reply);
      },
      base::Unretained(&reply_received), base::Unretained(&received_reply));

  // Prepare the status chain.
  hwsec::StatusChain<CryptohomeError> err1 = MakeStatus<CryptohomeError>(
      kTestLocation2, ErrorActionSet({ErrorAction::kTpmUpdateRequired}),
      user_data_auth::CryptohomeErrorCode::
          CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);

  hwsec::StatusChain<CryptohomeError> err2 =
      MakeStatus<CryptohomeError>(kTestLocation1,
                                  ErrorActionSet({ErrorAction::kReboot}))
          .Wrap(std::move(err1));

  // Make the call.
  user_data_auth::MountReply passedin_reply;
  passedin_reply.set_sanitized_username(kTestSanitizedUsername1);
  ReplyWithError(std::move(cb), passedin_reply, err2);

  // Check results.
  ASSERT_TRUE(reply_received);
  EXPECT_EQ(received_reply.error(),
            user_data_auth::CryptohomeErrorCode::
                CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR);
  ASSERT_TRUE(received_reply.has_error_info());
  EXPECT_EQ(received_reply.sanitized_username(), kTestSanitizedUsername1);
  EXPECT_EQ(received_reply.error_info().error_id(), "10001-10002");
  EXPECT_EQ(received_reply.error_info().primary_action(),
            user_data_auth::PrimaryAction::PRIMARY_TPM_UDPATE_REQUIRED);
  EXPECT_EQ(received_reply.error_info().possible_actions_size(), 0);
}

TEST_F(ErrorConverterTest, ReplyWithErrorSuccess) {
  // Prepare the callback.
  bool reply_received = false;
  user_data_auth::MountReply received_reply;
  auto cb = base::BindOnce(
      [](bool* reply_received_ptr,
         user_data_auth::MountReply* received_reply_ptr,
         const user_data_auth::MountReply& cb_reply) {
        ASSERT_FALSE(*reply_received_ptr);
        *reply_received_ptr = true;
        received_reply_ptr->CopyFrom(cb_reply);
      },
      base::Unretained(&reply_received), base::Unretained(&received_reply));

  // Prepare the status chain.
  hwsec::StatusChain<CryptohomeError> err1;

  // Make the call.
  user_data_auth::MountReply passedin_reply;
  passedin_reply.set_sanitized_username(kTestSanitizedUsername1);
  ReplyWithError(std::move(cb), passedin_reply, err1);

  // Check results.
  ASSERT_TRUE(reply_received);
  EXPECT_FALSE(received_reply.has_error_info());
  EXPECT_EQ(received_reply.error(),
            user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET);
  EXPECT_EQ(received_reply.sanitized_username(), kTestSanitizedUsername1);
}
}  // namespace

}  // namespace error

}  // namespace cryptohome
