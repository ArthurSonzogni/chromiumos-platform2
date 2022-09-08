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

#include "faced/face_auth_service_impl.h"
#include "faced/mojom/face_auth.mojom.h"

namespace faced {

namespace {

constexpr char kUserName[] = "someone@example.com";

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

using ::chromeos::face_auth::mojom::AuthenticationSessionConfig;
using ::chromeos::face_auth::mojom::CreateSessionResultPtr;
using ::chromeos::face_auth::mojom::EnrollmentSessionConfig;
using ::chromeos::face_auth::mojom::FaceAuthenticationService;
using ::chromeos::face_auth::mojom::FaceAuthenticationSessionDelegate;
using ::chromeos::face_auth::mojom::FaceEnrollmentSessionDelegate;
using ::chromeos::face_auth::mojom::SessionCreationError;
using ::chromeos::face_auth::mojom::SessionError;
using ::chromeos::face_auth::mojom::SessionInfo;

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

  FaceEnrollmentSessionDelegate delegate_impl;
  mojo::Receiver<FaceEnrollmentSessionDelegate> delegate(&delegate_impl);

  base::RunLoop run_loop;

  service->CreateEnrollmentSession(
      EnrollmentSessionConfig::New(SampleUserHash(), /*accessibility=*/false),
      delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));

  run_loop.Run();

  EXPECT_TRUE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestCreateAuthenticationSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  FaceAuthenticationSessionDelegate delegate_impl;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&delegate_impl);

  base::RunLoop run_loop;

  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));

  run_loop.Run();

  EXPECT_TRUE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestNoConcurrentSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  FaceAuthenticationSessionDelegate delegate_impl;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&delegate_impl);

  base::RunLoop first_run_loop;

  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        first_run_loop.Quit();
      }));

  first_run_loop.Run();

  EXPECT_TRUE(service_impl.has_active_session());

  FaceAuthenticationSessionDelegate second_delegate_impl;
  mojo::Receiver<FaceAuthenticationSessionDelegate> second_delegate(
      &delegate_impl);

  base::RunLoop second_run_loop;

  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      second_delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_error());
        EXPECT_EQ(SessionCreationError::ALREADY_EXISTS, result->get_error());
        second_run_loop.Quit();
      }));

  second_run_loop.Run();
}

TEST(FaceAuthServiceImpl, TestCancelSession) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  FaceAuthenticationSessionDelegate delegate_impl;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&delegate_impl);

  base::RunLoop run_loop;

  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        run_loop.Quit();
      }));

  run_loop.Run();

  EXPECT_TRUE(service_impl.has_active_session());

  // cancel session by disconnecting remote
  delegate.reset();

  RunUntil([&service_impl]() { return !service_impl.has_active_session(); });

  EXPECT_FALSE(service_impl.has_active_session());
}

TEST(FaceAuthServiceImpl, TestDisconnection) {
  bool disconnected = false;

  base::RunLoop first_run_loop;
  base::RunLoop second_run_loop;

  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::BindLambdaForTesting([&]() {
                                     disconnected = true;
                                     second_run_loop.Quit();
                                   }));

  FaceAuthenticationSessionDelegate delegate_impl;
  mojo::Receiver<FaceAuthenticationSessionDelegate> delegate(&delegate_impl);

  service->CreateAuthenticationSession(
      AuthenticationSessionConfig::New(SampleUserHash()),
      delegate.BindNewPipeAndPassRemote(),
      base::BindLambdaForTesting([&](CreateSessionResultPtr result) {
        EXPECT_TRUE(result->is_session_info());
        first_run_loop.Quit();
      }));

  first_run_loop.Run();

  EXPECT_TRUE(service_impl.has_active_session());

  service.reset();

  second_run_loop.Run();

  EXPECT_TRUE(disconnected);
  EXPECT_FALSE(service_impl.has_active_session());
}

}  // namespace faced
