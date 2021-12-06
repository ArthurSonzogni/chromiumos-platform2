// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/request.h"

#include <iostream>

namespace cros_im {
namespace test {

Request::~Request() = default;

bool Request::RequestMatches(const Request& actual) const {
  return type_ == actual.type_;
}

std::string Request::ToString() const {
  switch (type_) {
    case kDestroy:
      return "destroy()";
    case kActivate:
      return "activate()";
    case kDeactivate:
      return "deactivate()";
    case kHideInputPanel:
      return "hide_input_panel()";
    case kReset:
      return "reset()";
    case kSetSurroundingText:
      return "set_surrounding_text()";
    case kSetCursorRectangle:
      return "set_cursor_rectangle()";
  }
  return "[invalid request]";
}

std::ostream& operator<<(std::ostream& stream, const Request& request) {
  stream << "[Request: " << request.ToString() << "]";
  return stream;
}

}  // namespace test
}  // namespace cros_im
