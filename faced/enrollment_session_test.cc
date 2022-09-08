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

#include "faced/enrollment_session.h"
#include "faced/mock_face_enrollment_session_delegate.h"
#include "faced/mojom/face_auth.mojom.h"

namespace faced {

namespace {

constexpr char kUserName[] = "someone@example.com";

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

using ::chromeos::face_auth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::face_auth::mojom::EnrollmentSessionConfig;
using ::chromeos::face_auth::mojom::FaceEnrollmentSession;
using ::chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::face_auth::mojom::FaceOperationStatus;
using ::chromeos::face_auth::mojom::SessionError;

using ::brillo::cryptohome::home::SanitizeUserName;

std::string SampleUserHash() {
  return SanitizeUserName(kUserName);
}

absl::BitGen bitgen;

}  // namespace

TEST(TestSession, TestEnrollmentSessionComplete) {
  // Create a mock session delegate, that expects a completion event to be
  // triggered.
  StrictMock<MockFaceEnrollmentSessionDelegate> mock_delegate;
  EXPECT_CALL(mock_delegate, OnEnrollmentComplete(_))
      .WillOnce(Invoke([&](EnrollmentCompleteMessagePtr message) {
        EXPECT_EQ(message->status, FaceOperationStatus::OK);
      }));

  // Create an enrollment session.
  mojo::Receiver<FaceEnrollmentSessionDelegate> delegate(&mock_delegate);
  mojo::Remote<FaceEnrollmentSession> session_remote;
  auto session = EnrollmentSession::Create(
      bitgen, session_remote.BindNewPipeAndPassReceiver(),
      delegate.BindNewPipeAndPassRemote(),
      EnrollmentSessionConfig::New(SampleUserHash(), /*accessibility=*/false));

  // Set up a loop to run until the client disconnects.
  base::RunLoop run_loop;
  session.value()->RegisterDisconnectHandler(
      base::BindLambdaForTesting([&]() { run_loop.Quit(); }));

  // Notify the client is complete, and run the loop until the service
  // is disconnected.
  session.value()->NotifyComplete(FaceOperationStatus::OK);
  run_loop.Run();

  // On destruction, `mock_delegate` will ensure OnEnrollmentComplete
  // was called.
}

TEST(TestSession, TestEnrollmentSessionError) {
  // Create a mock session delegate, that expects an error event to be
  // triggered.
  StrictMock<MockFaceEnrollmentSessionDelegate> mock_delegate;
  EXPECT_CALL(mock_delegate, OnEnrollmentError(_))
      .WillOnce(Invoke([&](SessionError error) {
        EXPECT_EQ(error, SessionError::UNKNOWN);
      }));

  // Create an enrollment session.
  mojo::Receiver<FaceEnrollmentSessionDelegate> delegate(&mock_delegate);
  mojo::Remote<FaceEnrollmentSession> session_remote;
  auto session = EnrollmentSession::Create(
      bitgen, session_remote.BindNewPipeAndPassReceiver(),
      delegate.BindNewPipeAndPassRemote(),
      EnrollmentSessionConfig::New(SampleUserHash(), /*accessibility=*/false));

  // Set up a loop to run until the client disconnects.
  base::RunLoop run_loop;
  session.value()->RegisterDisconnectHandler(
      base::BindLambdaForTesting([&]() { run_loop.Quit(); }));

  // Notify the client of an internal error, and run the loop until the service
  // is disconnected.
  session.value()->NotifyError(absl::InternalError(""));
  run_loop.Run();

  // On destruction, `mock_delegate` will ensure OnEnrollmentError
  // was called.
}

}  // namespace faced
