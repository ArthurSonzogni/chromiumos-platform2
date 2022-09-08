// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>

#include <absl/random/random.h>
#include <absl/status/status.h>
#include <base/bind.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <brillo/cryptohome.h>
#include <gmock/gmock.h>
#include <gmock/gmock-nice-strict.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "faced/authentication_session.h"
#include "faced/mock_face_authentication_session_delegate.h"
#include "faced/mojom/face_auth.mojom.h"

namespace faced {

namespace {

constexpr char kUserName[] = "someone@example.com";

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

using ::chromeos::face_auth::mojom::AuthenticationCompleteMessagePtr;
using ::chromeos::face_auth::mojom::AuthenticationSessionConfig;
using ::chromeos::face_auth::mojom::FaceAuthenticationSession;
using ::chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::face_auth::mojom::FaceOperationStatus;
using ::chromeos::face_auth::mojom::SessionError;

using ::brillo::cryptohome::home::SanitizeUserName;

std::string SampleUserHash() {
  return SanitizeUserName(kUserName);
}

absl::BitGen bitgen;

}  // namespace

TEST(TestSession, TestAuthenticationSessionComplete) {
  // Create a mock session delegate, that expects a completion event to be
  // triggered.
  StrictMock<MockFaceAuthenticationSessionDelegate> mock_delegate;
  EXPECT_CALL(mock_delegate, OnAuthenticationComplete(_))
      .WillOnce(Invoke([](AuthenticationCompleteMessagePtr message) {
        EXPECT_EQ(message->status, FaceOperationStatus::OK);
      }));

  // Create an authentication session.
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&mock_delegate);
  auto session = AuthenticationSession::Create(
      bitgen, session_remote.BindNewPipeAndPassReceiver(),
      delegate.BindNewPipeAndPassRemote(),
      AuthenticationSessionConfig::New(SampleUserHash()));

  // Set up a loop to run until the client disconnects.
  base::RunLoop run_loop;
  session.value()->RegisterDisconnectHandler(
      base::BindLambdaForTesting([&]() { run_loop.Quit(); }));

  // Notify the client is complete, and run the loop until the service
  // is disconnected.
  session.value()->NotifyComplete(FaceOperationStatus::OK);
  run_loop.Run();

  // On destruction, `mock_delegate` will ensure OnAuthenticationComplete
  // was called.
}

TEST(TestSession, TestAuthenticationSessionError) {
  // Create a mock session delegate, that expects an error event to be
  // triggered.
  StrictMock<MockFaceAuthenticationSessionDelegate> mock_delegate;
  EXPECT_CALL(mock_delegate, OnAuthenticationError(_))
      .WillOnce(Invoke(
          [](SessionError error) { EXPECT_EQ(error, SessionError::UNKNOWN); }));

  // Create an authentication session.
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&mock_delegate);
  auto session = AuthenticationSession::Create(
      bitgen, session_remote.BindNewPipeAndPassReceiver(),
      delegate.BindNewPipeAndPassRemote(),
      AuthenticationSessionConfig::New(SampleUserHash()));

  // Set up a loop to run until the client disconnects.
  base::RunLoop run_loop;
  session.value()->RegisterDisconnectHandler(
      base::BindLambdaForTesting([&]() { run_loop.Quit(); }));

  // Notify the client of an internal error, and run the loop until the service
  // is disconnected.
  session.value()->NotifyError(absl::InternalError(""));
  run_loop.Run();

  // On destruction, `mock_delegate` will ensure OnAuthenticationError
  // was called.
}

}  // namespace faced
