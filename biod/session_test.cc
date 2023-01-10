
// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_manager.h"
#include "biod/session.h"

#include <gtest/gtest.h>

#include <memory>

namespace biod {
namespace {

TEST(SessionTest, EmptySessionError) {
  BiometricsManager::EnrollSession enroll_session;
  EXPECT_TRUE(enroll_session.error().empty());
}

TEST(SessionTest, SessionError) {
  std::string session_error = "HW is not available";
  BiometricsManager::EnrollSession enroll_session;
  enroll_session.set_error(session_error);
  EXPECT_EQ(enroll_session.error(), session_error);
}

}  // namespace
}  // namespace biod
