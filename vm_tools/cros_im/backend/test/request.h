// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_
#define VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_

#include <iostream>
#include <string>

namespace cros_im {
namespace test {

// Represents a Wayland request, i.e. a call to the compositor.
class Request {
 public:
  enum RequestType {
    // Requests on the text_input_manager
    kCreateTextInput,
    // Requests on a text_input object
    kDestroy,
    kActivate,
    kDeactivate,
    kHideInputPanel,
    kReset,
    kSetSurroundingText,
    kSetCursorRectangle,
  };

  Request(int text_input_id, RequestType type)
      : text_input_id_(text_input_id), type_(type) {}
  virtual ~Request();
  virtual bool RequestMatches(const Request& actual) const;
  virtual std::string ToString() const;

 private:
  friend std::ostream& operator<<(std::ostream& stream, const Request& request);
  int text_input_id_;
  RequestType type_;
};

std::ostream& operator<<(std::ostream& stream, const Request& request);

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_
