// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_client.h"

#include <gtest/gtest.h>

#include <string>

#include "faced/face_auth_service_impl.h"
#include "faced/mojom/faceauth.mojom.h"

#include "faced/util/blocking_future.h"

namespace faced {
namespace {

using ::chromeos::faceauth::mojom::FaceAuthenticationService;

constexpr char kUserName[] = "someone@example.com";

}  // namespace

TEST(EnrollWithRemoteService, EnrollCompletesCorrectly) {
  mojo::Remote<FaceAuthenticationService> service;
  FaceAuthServiceImpl service_impl(service.BindNewPipeAndPassReceiver(),
                                   base::OnceClosure());

  BlockingFuture<absl::Status> enrollment_result;

  EnrollWithRemoteService(kUserName, service,
                          enrollment_result.PromiseCallback());

  EXPECT_TRUE(enrollment_result.Wait().ok());
}

}  // namespace faced
