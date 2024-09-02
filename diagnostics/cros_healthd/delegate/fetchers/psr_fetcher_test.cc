// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/psr_fetcher.h"

#include <gtest/gtest.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

TEST(PsrFetcherTest, ConvertLogStateToMojo) {
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kNotStarted),
            mojom::PsrInfo::LogState::kNotStarted);
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kStarted),
            mojom::PsrInfo::LogState::kStarted);
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kStopped),
            mojom::PsrInfo::LogState::kStopped);
}

TEST(PsrFetcherTest, ConvertPsrEventTypeToMojo) {
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kLogStart),
            mojom::PsrEvent::EventType::kLogStart);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kLogEnd),
            mojom::PsrEvent::EventType::kLogEnd);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kMissing),
            mojom::PsrEvent::EventType::kMissing);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kInvalid),
            mojom::PsrEvent::EventType::kInvalid);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kPrtcFailure),
            mojom::PsrEvent::EventType::kPrtcFailure);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeRecovery),
            mojom::PsrEvent::EventType::kCsmeRecovery);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeDamState),
            mojom::PsrEvent::EventType::kCsmeDamState);
  EXPECT_EQ(
      internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeUnlockState),
      mojom::PsrEvent::EventType::kCsmeUnlockState);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kSvnIncrease),
            mojom::PsrEvent::EventType::kSvnIncrease);
  EXPECT_EQ(
      internal::ConvertPsrEventTypeToMojo(psr::EventType::kFwVersionChanged),
      mojom::PsrEvent::EventType::kFwVersionChanged);
}

}  // namespace
}  // namespace diagnostics
