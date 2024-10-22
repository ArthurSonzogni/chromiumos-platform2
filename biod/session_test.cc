// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/session.h"

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "biod/biometrics_manager.h"
#include "biod/mock_biometrics_manager.h"

namespace biod {
namespace {

TEST(SessionTest, MoveConstructor) {
  MockBiometricsManager mock_biometrics_manager;
  BiometricsManager::EnrollSession enroll_session_1(
      mock_biometrics_manager.session_weak_factory_.GetWeakPtr());

  ASSERT_TRUE(enroll_session_1);

  BiometricsManager::EnrollSession enroll_session_2(
      std::move(enroll_session_1));
  EXPECT_FALSE(enroll_session_1);
  EXPECT_TRUE(enroll_session_2);
}

TEST(SessionTest, MoveAssignment) {
  MockBiometricsManager mock_biometrics_manager;

  BiometricsManager::EnrollSession enroll_session_1(
      mock_biometrics_manager.session_weak_factory_.GetWeakPtr());
  BiometricsManager::EnrollSession enroll_session_2;

  ASSERT_TRUE(enroll_session_1);
  ASSERT_FALSE(enroll_session_2);

  enroll_session_2 = std::move(enroll_session_1);
  EXPECT_FALSE(enroll_session_1);
  EXPECT_TRUE(enroll_session_2);
}

TEST(SessionTest, EndValidSession) {
  MockBiometricsManager mock_biometrics_manager;

  BiometricsManager::EnrollSession enroll_session_1(
      mock_biometrics_manager.session_weak_factory_.GetWeakPtr());

  ASSERT_TRUE(enroll_session_1);
  enroll_session_1.End();
  EXPECT_FALSE(enroll_session_1);
}

TEST(SessionTest, EndInvalidSession) {
  BiometricsManager::EnrollSession enroll_session_1;

  ASSERT_FALSE(enroll_session_1);
  enroll_session_1.End();
  EXPECT_FALSE(enroll_session_1);
}

}  // namespace
}  // namespace biod
