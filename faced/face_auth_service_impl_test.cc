// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <gmock/gmock.h>
#include <gmock/gmock-nice-strict.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "faced/face_auth_service_impl.h"
#include "faced/mock_face_authentication_session_delegate.h"
#include "faced/mock_face_enrollment_session_delegate.h"
#include "faced/mojom/faceauth.mojom.h"

namespace faced {

namespace {

constexpr char kUserName[] = "someone@example.com";

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

using ::chromeos::faceauth::mojom::AuthenticationCompleteMessagePtr;
using ::chromeos::faceauth::mojom::AuthenticationSessionConfig;
using ::chromeos::faceauth::mojom::AuthenticationUpdateMessagePtr;
using ::chromeos::faceauth::mojom::CreateSessionResultPtr;
using ::chromeos::faceauth::mojom::EnrollmentCompleteMessagePtr;
using ::chromeos::faceauth::mojom::EnrollmentSessionConfig;
using ::chromeos::faceauth::mojom::EnrollmentUpdateMessagePtr;
using ::chromeos::faceauth::mojom::FaceAuthenticationService;
using ::chromeos::faceauth::mojom::FaceAuthenticationSession;
using ::chromeos::faceauth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::faceauth::mojom::FaceEnrollmentSession;
using ::chromeos::faceauth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::faceauth::mojom::SessionCreationError;
using ::chromeos::faceauth::mojom::SessionError;
using ::chromeos::faceauth::mojom::SessionInfo;

using ::brillo::cryptohome::home::SanitizeUserName;

std::string SampleUserHash() {
  return SanitizeUserName(kUserName);
}

void RunUntil(std::function<bool()> check,
              base::TimeDelta timeout = base::Minutes(1)) {
  base::TimeTicks start_time(base::TimeTicks::Now());

  // Run the loop.
  base::RunLoop().RunUntilIdle();

  // While the condition hasn't become true, sleep for a
  // short duration, and then check again.
  while (!check() && (base::TimeTicks::Now() - start_time) < timeout) {
    base::PlatformThread::Sleep(base::Milliseconds(10));
    base::RunLoop().RunUntilIdle();
  }
}

}  // namespace

TEST(FaceAuthServiceImpl, TestCreateEnrollmentSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  // Create a mock session delegate.
  StrictMock<MockFaceEnrollmentSessionDelegate> delegate;

  // Request the service to begin an enrollment session.
  base::RunLoop run_loop;
  mojo::Remote<FaceEnrollmentSession> session_remote;
  mojo::Receiver<FaceEnrollmentSessionDelegate> receiver(&delegate);
  service->CreateEnrollmentSession(
      EnrollmentSessionConfig::New(SampleUserHash(), /*accessibility=*/false),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));
  run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestCancelEnrollmentSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  // Create a mock session delegate, that expects a cancellation event to be
  // triggered.
  StrictMock<MockFaceEnrollmentSessionDelegate> delegate;
  EXPECT_CALL(delegate, OnEnrollmentCancelled()).Times(1);

  // Request the service to begin an enrollment session.
  base::RunLoop run_loop;
  mojo::Remote<FaceEnrollmentSession> session_remote;
  mojo::Receiver<FaceEnrollmentSessionDelegate> receiver(&delegate);
  service->CreateEnrollmentSession(
      EnrollmentSessionConfig::New(SampleUserHash(), /*accessibility=*/false),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));
  run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());

  // Cancel the session by disconnecting `session_remote`.
  session_remote.reset();

  // Wait for `service_impl` to report that there is no longer an active
  // session.
  RunUntil([&service_impl]() { return !service_impl.has_active_session(); });
  EXPECT_FALSE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestCreateAuthenticationSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  // Create a mock session delegate.
  StrictMock<MockFaceAuthenticationSessionDelegate> delegate;

  // Request the service to begin an authentication session.
  base::RunLoop run_loop;
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> receiver(&delegate);
  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));
  run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestNoConcurrentSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  // Create a mock session delegate.
  StrictMock<MockFaceAuthenticationSessionDelegate> delegate;

  // Request the service to begin an authentication session.
  base::RunLoop first_run_loop;
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> receiver(&delegate);
  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        first_run_loop.Quit();
      }));
  first_run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());

  // Create a second mock session delegate.
  StrictMock<MockFaceAuthenticationSessionDelegate> second_delegate;

  // Request the service to begin a second authentication session.
  base::RunLoop second_run_loop;
  mojo::Remote<FaceAuthenticationSession> second_session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> second_receiver(
      &second_delegate);
  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      second_session_remote.BindNewPipeAndPassReceiver(),
      second_receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_error());
        EXPECT_EQ(SessionCreationError::ALREADY_EXISTS, result->get_error());
        second_run_loop.Quit();
      }));
  second_run_loop.Run();
}

TEST(FaceAuthServiceImpl, TestCancelAuthenticationSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  // Create a mock session delegate.
  StrictMock<MockFaceAuthenticationSessionDelegate> delegate;
  EXPECT_CALL(delegate, OnAuthenticationCancelled()).Times(1);

  // Request the service to begin an authentication session.
  base::RunLoop run_loop;
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> receiver(&delegate);
  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));
  run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());

  // Cancel the session by disconnecting `session_remote`.
  session_remote.reset();
  RunUntil([&service_impl]() { return !service_impl.has_active_session(); });

  EXPECT_FALSE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestDisconnection) {
  base::RunLoop second_run_loop;

  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::BindLambdaForTesting([&]() {
                                     second_run_loop.Quit();
                                   }));

  // Create a mock session delegate.
  StrictMock<MockFaceAuthenticationSessionDelegate> delegate;

  // Request the service to begin an authentication session.
  base::RunLoop run_loop;
  mojo::Remote<FaceAuthenticationSession> session_remote;
  mojo::Receiver<FaceAuthenticationSessionDelegate> receiver(&delegate);
  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      session_remote.BindNewPipeAndPassReceiver(),
      receiver.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));
  run_loop.Run();

  // Ensure the service indicates a session is active.
  EXPECT_TRUE(service_impl.has_active_session());

  // End the session by disconnecting `service`.
  service.reset();

  second_run_loop.Run();

  // Ensure the service indicates a session is inactive.
  EXPECT_FALSE(service_impl.has_active_session());
}

}  // namespace faced
