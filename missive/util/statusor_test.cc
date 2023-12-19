// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/statusor.h"

#include <utility>

#include <base/types/expected.h>
#include <gtest/gtest.h>

#include "missive/util/status.h"

namespace reporting {
namespace {

TEST(StatusOr, MoveConstructFromAndExtractToStatusImplicitly) {
  Status status(error::INTERNAL, "internal error");
  base::unexpected<Status> unexpected_status(status);
  StatusOr<int> status_or(std::move(unexpected_status));
  Status extracted_status{std::move(status_or).error()};
  EXPECT_EQ(status, extracted_status);
}

TEST(StatusOr, CopyConstructFromAndExtractToStatusImplicitly) {
  Status status(error::INTERNAL, "internal error");
  base::unexpected<Status> unexpected_status(status);
  StatusOr<int> status_or(unexpected_status);
  Status extracted_status{status_or.error()};
  EXPECT_EQ(status, extracted_status);
}

}  // namespace
}  // namespace reporting
