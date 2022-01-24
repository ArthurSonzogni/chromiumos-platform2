// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/event.h"

#include <iostream>

#include "backend/test/backend_test.h"
#include "backend/test/backend_test_utils.h"
#include "backend/test/mock_text_input.h"

namespace cros_im {
namespace test {

std::ostream& operator<<(std::ostream& stream, const Event& event) {
  stream << "[Event: ";
  event.Print(stream);
  stream << "]";
  return stream;
}

CommitStringEvent::~CommitStringEvent() = default;

void CommitStringEvent::Run() const {
  auto* text_input = GetTextInput(text_input_id_);
  if (!text_input) {
    FAILED() << "Failed to find text_input object";
    return;
  }
  text_input->listener->commit_string(text_input->listener_data, text_input,
                                      /*serial=*/0, text_.c_str());
}

void CommitStringEvent::Print(std::ostream& stream) const {
  stream << "commit_string(" << text_ << ")";
}

KeySymEvent::~KeySymEvent() = default;

void KeySymEvent::Run() const {
  auto* text_input = GetTextInput(text_input_id_);
  if (!text_input) {
    FAILED() << "Failed to find text_input object";
    return;
  }
  bool pressed = true;
  text_input->listener->keysym(text_input->listener_data, text_input,
                               /*serial=*/0, /*time=*/0, keysym_,
                               /*state=*/pressed, /*modifiers=*/0);
}

void KeySymEvent::Print(std::ostream& stream) const {
  stream << "key_sym(" << keysym_ << ")";
}

}  // namespace test
}  // namespace cros_im
