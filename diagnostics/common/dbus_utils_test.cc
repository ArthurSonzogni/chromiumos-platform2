// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/dbus_utils.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace diagnostics {

namespace {
const int TEST_INT = 42;
const float TEST_FLOAT = 3.14;
const auto TEST_ERR = brillo::Error::Create(base::Location(), "", "", "");
}  // namespace

class MockHandler {
 public:
  MOCK_METHOD(void, Handle, (brillo::Error*, int, float));
};

TEST(DbusUtilsTest, SplitBusCallbackOnSuccess) {
  MockHandler handler;
  EXPECT_CALL(handler, Handle(nullptr, TEST_INT, TEST_FLOAT)).Times(1);

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&MockHandler::Handle, base::Unretained(&handler)));
  std::move(on_success).Run(TEST_INT, TEST_FLOAT);
}

TEST(DbusUtilsTest, SplitBusCallbackOnError) {
  MockHandler handler;
  EXPECT_CALL(handler, Handle(TEST_ERR.get(), int(), float())).Times(1);

  auto [on_success, on_error] = SplitDbusCallback(
      base::BindOnce(&MockHandler::Handle, base::Unretained(&handler)));
  std::move(on_error).Run(TEST_ERR.get());
}

}  // namespace diagnostics
