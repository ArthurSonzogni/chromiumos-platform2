// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_SCREEN_H_
#define MINIOS_MOCK_SCREEN_H_

#include <string>

#include <brillo/errors/error.h>
#include <gmock/gmock.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/screen_types.h"
#include "minios/screens/screen_base.h"

namespace minios {

class MockScreen : public ScreenInterface {
 public:
  MockScreen() = default;
  ~MockScreen() override = default;

  MockScreen(const MockScreen&) = delete;
  MockScreen& operator=(const MockScreen&) = delete;

  MOCK_METHOD(void, Show, ());
  MOCK_METHOD(void, OnKeyPress, (int key_changed));
  MOCK_METHOD(void, Reset, ());
  MOCK_METHOD(ScreenType, GetType, ());
  MOCK_METHOD(std::string, GetName, ());
  MOCK_METHOD(State, GetState, ());
  MOCK_METHOD(bool, MoveForward, (brillo::ErrorPtr * err));
  MOCK_METHOD(bool, MoveBackward, (brillo::ErrorPtr * err));
};

}  // namespace minios

#endif  // MINIOS_MOCK_SCREEN_H_
